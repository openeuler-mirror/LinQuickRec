# Perf 系统设计文档

## 1. 概述

### 1.1 目的

LinQuickRec 当前通过 `common::perf::Log()` 将每条请求的各阶段耗时写入本地日志文件。这种方案存在四个缺陷：(1) 同步 `std::endl` 写入阻塞业务线程；(2) 日志轮转（500MB）后旧数据永久丢失；(3) 各服务日志分散在不同 Pod 中，无法按 trace_id 关联完整请求链路；(4) 无实时聚合能力，无法在运行时查询 avg/p99。

Perf 系统的目标是：**以 \<1μs 的边际成本记录每条 span，提供实时聚合、持久化存储、全链路追踪、离群值检测和可视化仪表盘，且 collector 宕机不影响业务服务。**

### 1.2 方式

- **记录层**：在每个业务服务的进程内，`perf::Log()` 将 span 写入 lock-free ring buffer（CAS 原子操作），取代同步文件写入
- **采集层**：独立的 `perf-collector` 服务通过 BRPC 定时拉取各服务的 span 数据
- **聚合层**：Welford 增量算法实时计算 avg/stddev/p50/p99，不存储全量数据
- **存储层**：SQLite WAL 模式批量持久化，30 天留存
- **服务发现**：perf-collector 通过 etcd 或 discovery_server 自动发现下游服务（proxy/feature/recall/precalc-and-rank-master/rank-sub），无需硬编码地址

### 1.3 Architecture

```
+--- each service pod ----------------------------------------------------+
|                                                                         |
|  perf::Log(service,stage,metric,tid,ms)                                 |
|    +-- PerfRingBuffer::Push(Span)   <-- CAS, ~1ns, no blocking         |
|                                                                         |
|  /debug/perf (BRPC service)                                            |
|    +-- PerfRingBuffer::Snapshot() -> Protobuf response                 |
|                                                                         |
+------------------------------------+------------------------------------+
                                     |  BRPC call (1s interval)
                                     |  via ServiceDiscovery
                                     v
+-- perf-collector pod ---------------------------------------------------+
|                                                                         |
|  +----------+    +---------------+    +--------------+    +----------+  |
|  |  Puller  | -> |  StatsEngine  | -> |  SqliteStore | -> |  API     |  |
|  | (1s loop)|    |   (Welford)   |    |    (WAL)     |    | Handlers |  |
|  +----------+    +-------+-------+    +--------------+    +-----+----+  |
|                          |                                       |      |
|                    +-----+-----+                          +-----+---+  |
|                    |  Outlier  |                          |  WebUI  |  |
|                    | Detector  |                          | (static)|  |
|                    | (z-score) |                          +---------+  |
|                    +-----------+                                      |
|                                                                         |
|  +-- SeriesManager --------------------------------------------------+  |
|  |    POST /start?name=baseline  -> tag subsequent spans series_id    |  |
|  |    POST /stop                 -> freeze series, query stats only   |  |
|  +--------------------------------------------------------------------+  |
|                                                                         |
+-------------------------------------------------------------------------+

Data flow:
  Puller -> StatsEngine (incremental stats) -> SqliteStore (persist)
         -> OutlierDetector (flag anomalies)
         -> SeriesManager (tag active series spans)
         -> API Handlers (unified query endpoint)
```

## 2. 模块布局

### 2.1 `common/` — 记录层（所有服务复用）

| 文件 | 内容 | 职责 |
|------|------|------|
| `common/include/common/perf_registry.h` | `struct Span`, `class PerfRingBuffer`, `class PerfRingRegistry` 声明 | 头文件：数据结构定义、接口声明。`Push()` 为 inline（单次 CAS），其余方法声明 |
| `common/src/perf/perf_registry.cpp` | `PerfRingBuffer::Snapshot()`, `PerfRingRegistry::Init()` 实现 | 实现文件：序列化 ring buffer 内容为 span 列表、单例初始化 |
| `common/include/common/perf_logger.h` | `perf::Log()` 函数（修改，末尾追加 `Registry::Push`） | 头文件（inline）：格式化 PERF 行后调用 `PerfRingRegistry::Instance().Push(span)` |

两者关系：`perf_logger.h` 是记录入口，`perf_registry.h` 是存储后端。前者 include 后者。每个业务服务编译时链接 `common` 即可同时获得两者。`perf_registry.cpp` 的编译产物包含在 `common_lib` 静态库中。

### 2.2 各业务服务（proxy, feature, recall, precalc-and-rank-master, rank-sub）

| 位置 | 内容 | 职责 |
|------|------|------|
| `main.cpp` | 注册 `/debug/perf` BRPC service | handler 调用 `PerfRingRegistry::Instance().Snapshot()`，返回 Protobuf 响应 |
| 进程内存 | `PerfRingRegistry` 单例（50K 条，~6MB） | 缓存 span，等待 collector 拉取 |

每个业务服务**不依赖** perf-collector。collector 宕机时 ring buffer 自旋覆盖旧数据，业务服务不受影响。

### 2.3 `services/perf/collector/` — 采集 + 聚合 + 存储层

| 文件 | 职责 |
|------|------|
| `src/main.cpp` | BRPC server 启动 + Puller 启动 |
| `src/puller.cpp` | 每秒通过 ServiceDiscovery 发现各服务，BRPC 调用 `/debug/perf` 拉取 span |
| `src/stats_engine.cpp` | `WelfordRunningStats` 类：增量更新 avg/variance + reservoir sampling 估算分位数 |
| `src/sqlite_store.cpp` | 建表、批量 INSERT（1000 行/事务）、留存清理（30 天） |
| `src/outlier_detector.cpp` | 每次 Flush 后对每个 stage 计算 z-score，标记 `is_outlier=1` |
| `src/series_manager.cpp` | 管理请求系列的 start/stop 生命周期，对 span 打 `series_id` 标签 |
| `src/api_handlers.cpp` | REST API handler（stats / trace / series / outlier / health） |

### 2.4 `services/perf/webui/` — 展示层

纯静态前端，通过 `fetch()` 调用 collector 的 REST API，Chart.js 渲染图表

## 3. 数据结构与算法

### 3.1 PerfRingBuffer（services/perf/common/perf_registry.h）

**数据结构**：预分配数组 + 两个原子计数器。

```cpp
struct Span {
    uint64_t ts_us;          // 微秒时间戳
    char     service[16];    // "proxy"
    char     stage[32];      // "proxy_e2e"
    char     metric[16];     // "processing"
    char     trace_id[33];   // 32-char hex
    float    duration_ms;
    char     status[8];      // "ok"/"error"
};

class PerfRingBuffer {
    std::atomic<uint64_t> write_pos_{0};   // monotonic, 写者递增
    std::atomic<uint64_t> read_pos_{0};    // 消费者更新（collector pull 后）
    std::atomic<uint64_t> dropped_{0};     // 溢出计数
    size_t capacity_;                      // 默认 50000
    Span* buffer_;                         // 预分配，生命周期 = 进程
};
```

**Push 算法**（业务线程调用，lock-free）：

```
1. pos = write_pos_.fetch_add(1, memory_order_relaxed)   // 原子获取写入位置
2. if (pos - read_pos_.load() >= capacity_)               // 写指针超出读指针超过容量
       dropped_.fetch_add(1)                               // FIFO 覆盖，丢弃计数 +1
3. buffer_[pos % capacity_] = span                        // 写入 span（不阻塞、不分配内存）
```

**Snapshot 算法**（collector BRPC handler 调用）：

```
1. w = write_pos_.load(memory_order_acquire)
2. r = read_pos_.load()
3. for i in [r, w): result.push_back(buffer_[i % capacity_])
4. read_pos_.store(w, memory_order_release)               // 提交消费位置
5. return {spans: result, dropped: dropped_.exchange(0)}   // 返回 span + 清零 dropped
```

**复杂度**：Push O(1), Snapshot O(k) where k = write_pos - read_pos。写入路径无锁、无堆分配、无系统调用。

### 3.2 WelfordRunningStats（services/perf/collector/src/stats_engine.cpp）

**目的**：增量计算平均值和方差，不需要存储全部样本。

**算法**（Knuth/Welford）：

```
对每个新值 x:
    count += 1
    delta  = x - mean
    mean  += delta / count
    delta2 = x - mean
    M2    += delta * delta2     // M2 = sum of squared differences

结果:
    avg    = mean
    var    = M2 / count
    stddev = sqrt(var)
```

**分位数估算**：Reservoir Sampling（1%）。维护一个固定大小（N/100）的 reservoir 数组，对每个新值以 1% 概率替换 reservoir 中的一个随机元素。分位数通过排序 reservoir 后取对应索引。

**复杂度**：Push O(1)（5 次浮点运算 + 1% 概率数组替换）。

### 3.3 OutlierDetector（services/perf/collector/src/outlier_detector.cpp）

**算法**：z-score 方法。对每个 stage 维护一个 WelfordRunningStats。

```
判定: |x - avg| > threshold × stddev → is_outlier = true
threshold 可配置，默认 3.0（3σ 规则）
```

每次 SqliteStore Flush 后触发检测，标记对应行 `is_outlier = 1`。

### 3.4 SQLite Schema（services/perf/collector/src/sqlite_store.cpp）

```sql
CREATE TABLE spans (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_us      INTEGER NOT NULL,
    service    TEXT NOT NULL,
    stage      TEXT NOT NULL,
    metric     TEXT NOT NULL,
    trace_id   TEXT NOT NULL,
    duration_ms REAL NOT NULL,
    status     TEXT NOT NULL,
    series_id  INTEGER DEFAULT NULL,
    is_outlier INTEGER DEFAULT 0
);

CREATE INDEX idx_ts       ON spans(ts_us);
CREATE INDEX idx_stage_ts ON spans(stage, ts_us);
CREATE INDEX idx_trace    ON spans(trace_id);

CREATE TABLE series (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT NOT NULL,
    start_ts   INTEGER NOT NULL,
    stop_ts    INTEGER,
    status     TEXT NOT NULL DEFAULT 'active'
);

PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA cache_size=-64000;
```

**写入策略**：批量 INSERT（1000 行/事务），每 1 秒 commit 一次。WAL 模式下读写不互斥。

**清理策略**：每天执行 `DELETE FROM spans WHERE ts_us < strftime('%s', 'now', '-30 days') * 1000000;`。

### 3.5 SeriesManager（services/perf/collector/src/series_manager.cpp）

**状态机**：

```
POST /start → status=active, start_ts=now
POST /stop  → status=stopped, stop_ts=now
     ├── 所有从 start_ts 到 stop_ts 的 span 标记 series_id
     └── stats 查询时 WHERE series_id = ? GROUP BY stage
```

以 `make_unique<Series>` 内存管理活跃系列，`stop` 时持久化到 SQLite。重启时从 SQLite 恢复所有系列。

## 4. 拉取协议

### 4.1 服务端（各业务 Pod）

在 `brpc::Server` 中注册一个 BRPC service，method 为 `/debug/perf`：

```protobuf
message PerfSnapshotRequest {}
message PerfSpan {
    uint64 ts_us = 1;
    string service = 2;
    string stage = 3;
    string metric = 4;
    string trace_id = 5;
    double duration_ms = 6;
    string status = 7;
}
message PerfSnapshotResponse {
    repeated PerfSpan spans = 1;
    uint64 dropped = 2;
}
```

Handler 实现：调用 `PerfRingRegistry::Instance().Snapshot()` → 填充 Protobuf 响应。

### 4.2 客户端（perf-collector Puller）

```
Puller::Run() {
    while (running_) {
        for (auto& svc : discover_services()) {
            channel.Init(svc.addr, ...);
            stub.Snapshot(&cntl, &req, &rsp, nullptr);
            if (!cntl.Failed()) {
                for (auto& span : rsp.spans()) {
                    stats_engine_->Push(span);
                    sqlite_batch_.push_back(span);
                }
            }
        }
        sqlite_store_->Flush(sqlite_batch_);
        sleep(pull_interval_ms_);
    }
}
```

服务列表通过 `common::ServiceDiscovery` 动态获取。每次拉取后立即提交 batch 到 SQLite。

## 5. 服务发现的集成

perf-collector 复用现有的 `common::ServiceDiscovery`（与 Proxy 相同的方式）。启动时配置 `--registry_backend=etcd --etcd_endpoints=etcd-client:2379`。

拉取的目标服务列表通过 service discovery 的 naming service 获取（各服务注册的 `/debug/perf` 以 service type 区分）。collector 不需要知道具体 Pod IP，ServiceDiscovery 自动做负载均衡和 failover。
