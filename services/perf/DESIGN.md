# Perf 系统设计文档

## 0. 需求描述

| 编号 | 需求 | 优先级 |
|------|------|:---:|
| **R1** | 各服务进程内以零阻塞方式记录 **Span（跨度）**——每次 `perf::Log()` 调用 < 1μs 的 CPU 开销 | P0 |
| **R2** | **collector（采集器）** 容器化部署，定时（1s）通过 BRPC 从各服务拉取 span，内存中实时计算 avg/p50/p99/stddev | P0 |
| **R3** | Span 数据通过 **SQLite（嵌入式关系数据库）** WAL 模式持久化，留存时间可配置（通过 `--retention_days` 启动参数，0 = 永久留存） | P0 |
| **R4** | collector 容器通过 PVC 挂载数据目录，Pod 删除重启后历史数据不丢失 | P1 |
| **R5** | REST API：`GET /api/v1/stats/current`（实时统计）、`GET /api/v1/trace/{trace_id}`（全链路查询）、`GET /api/v1/health` | P1 |
| **R6** | **Series（请求系列）** 管理：`POST /start` → 发请求 → `POST /stop` → 按系列独立查询统计值，支撑 A/B 对比 | P1 |
| **R7** | 自动检测 **Outlier（离群值）**：z-score 方法，阈值可配置，通过 API 查询 | P2 |
| **R8** | **WebUI（网页仪表盘）** 实时展示 avg/p50/p99，提供 series 选择和 trace 查询，3s 自动刷新 | P1 |
| **R9** | WebUI Series 详情页：展示该系列各 stage 的统计值 + 逐条 span 数据 | P2 |
| **R10** | WebUI Trace 查询页：输入 **trace_id（追踪标识符）** 查看完整 Span 链路瀑布图 | P2 |

## 1. 概述

### 1.1 目的

LinQuickRec 当前通过 `common::perf::Log()` 将每条请求的各阶段耗时写入本地日志文件。这种方案存在四个缺陷：(1) 同步 `std::endl` 写入阻塞业务线程；(2) 日志轮转（500MB）后旧数据永久丢失；(3) 各服务日志分散在不同 Pod 中，无法按 **trace_id（追踪标识符）** 关联完整请求链路；(4) 无实时聚合能力，无法在运行时查询 avg/p99。

Perf 系统的目标是：**以 \<1μs 的边际成本记录每条 **Span（跨度）**，提供实时聚合、持久化存储、全链路追踪、**Outlier（离群值）** 检测和 **WebUI（网页仪表盘）**，且 **collector（采集器）** 宕机不影响业务服务。**

### 1.2 方式

- **记录层**：在每个业务服务的进程内，`perf::Log()` 将 **Span（跨度）** 写入 lock-free **RingBuffer（环形缓冲区）**（**CAS（比较并交换）** 原子操作），取代同步文件写入
- **采集层**：独立的 `perf-collector` 服务通过 **BRPC（百度 RPC 框架）** 定时拉取各服务的 span 数据
- **聚合层**：**Welford 算法** 增量实时计算 avg/stddev/p50/p99，P50/P99 通过 **Reservoir Sampling（蓄水池抽样）** 估算
- **存储层**：**SQLite（嵌入式关系数据库）** **WAL（预写日志模式）** 批量持久化，留存时间可配置
- **服务发现**：perf-collector 通过 etcd 或 discovery_server 自动发现下游服务（proxy/feature/recall/precalc-and-rank-master/rank-sub），无需硬编码地址
- **聚合依据**：单条请求通过 **trace_id（追踪标识符）** 追踪。使用 **Series（请求系列）** 标识一个测试组内的若干条请求，按 series 计算统计值，不重启即可切换系列

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

## 2. 术语表

| 术语 | 中文全称 | 释义 |
|------|---------|------|
| **Span** | 跨度 | 一条性能数据点。记录了一个服务（service）的某个处理阶段（stage）的耗时（duration_ms），附带 **trace_id（追踪标识符）**、时间戳、状态等元数据。一条 Proxy 请求会产生约 9 条 span。 |
| **trace_id** | 追踪标识符 | 32 字符的十六进制字符串，由 Proxy 入口生成（前 16 字符 = 微秒时间戳，后 16 字符 = 随机数）。通过 **BRPC（百度 RPC 框架）** 的 `cntl.set_log_id()` 传递到所有下游服务，用于关联同一请求跨服务的所有 **Span（跨度）**。 |
| **RingBuffer** | 环形缓冲区 | 预分配固定大小数组 + 两个原子计数器的数据结构。写入可无锁，读取快照时无阻塞且无额外内存分配。当写入超过容量时触发 FIFO 覆盖（最旧数据被新数据覆盖），同时 `dropped` 计数器递增。 |
| **PerfRingRegistry** | 全局性能注册表 | 进程级单例，持有唯一的一个 **RingBuffer（环形缓冲区）** 实例。服务启动时调用 `Init(50000)` 创建，`perf::Log()` 内部调用其 `Push()` 记录 **Span（跨度）**。未初始化时 `Push()` 为 no-op。 |
| **collector** | 采集器 | `perf-collector` 服务进程。通过 **BRPC（百度 RPC 框架）** 以 1 秒为周期拉取各业务服务的 **Span（跨度）** 数据，送入 **StatsEngine（统计引擎）** 聚合和 **SQLite（嵌入式关系数据库）** 持久化，并通过 REST API 对外暴露查询。 |
| **Puller** | 拉取器 | **collector（采集器）** 内部的拉取循环模块。每秒遍历目标服务列表，通过 **ServiceDiscovery（服务发现）** 获取各服务的地址，BRPC HTTP GET `/debug/perf`，解析 JSON 响应。 |
| **StatsEngine** | 统计引擎 | 使用 **Welford 算法** 增量计算各 stage 的平均值、方差和标准差。通过 **Reservoir Sampling（蓄水池抽样）** 维护 1000 个样本估算 P50/P99 分位数。 |
| **Welford 算法** | Welford 在线统计算法 | 一种 O(1) 增量的在线统计算法。每接收一个新样本值 x，只做 5 次浮点运算即可更新均值和方差，不需要存储全部历史样本。由 Knuth 推广，Welford 在 1962 年提出。 |
| **Series** | 请求系列 | 用户通过 API 手动标记的一段时间区间。调用 `POST /series/start` 开始，`POST /series/stop` 结束。区间内的所有 **Span（跨度）** 被打上 `series_id` 标签，之后可按系列独立查询统计值，用于 A/B 对比或基线分析。 |
| **Outlier** | 离群值 | 耗时显著偏离正常分布的异常请求。判定标准：`|duration_ms - avg| > threshold × stddev`（默认 threshold = 3，即 3σ 规则）。被标记为离群值的 span 存储 `is_outlier=1` 字段，可通过 API 查询。 |
| **Reservoir Sampling** | 蓄水池抽样 | 一种在线随机抽样算法。维护一个固定大小为 K 的蓄水池数组，第 N 个样本有 `K/N` 的概率替换蓄水池中的某个随机元素。在不知道总样本数的情况下保证每个样本被选中的概率相等。 |
| **SQLite WAL** | SQLite 预写日志模式 | SQLite 的一种并发优化模式（Write-Ahead Logging）。写操作追加到 WAL 文件末尾不阻塞读，后台 checkpoint 线程将 WAL 合并回主数据库文件。读写不互斥。 |
| **ServiceDiscovery** | 服务发现 | 项目的服务发现抽象层（`common::ServiceDiscovery`），支持 etcd 和自建 discovery_server 两种后端。消费者通过 `GetInstance(service_name)` 获取实例列表（round-robin 选取）。 |
| **BRPC** | 百度 RPC 框架 | 本项目使用的 C++ RPC 框架。支持多种协议（protobuf / HTTP / redis 等），内置连接池、负载均衡、健康检查。 |
| **CAS** | 比较并交换 | 一种无锁并发原语。`std::atomic<uint64_t>::fetch_add(1)` 内部使用 CPU 的 `LOCK CMPXCHG` 指令。比 `mutex::lock()` 快 2-3 个数量级，是 **RingBuffer（环形缓冲区）** 的 `Push()` 能达到 ~1ns 的关键。 |

## 3. 模块布局

### 3.1 `common/` — 记录层（所有服务复用）

| 文件 | 内容 | 职责 |
|------|------|------|
| `common/include/common/perf_registry.h` | `struct Span`, `class PerfRingBuffer`, `class PerfRingRegistry` 声明 | 头文件：数据结构定义、接口声明。`Push()` 为 inline（单次 **CAS（比较并交换）**），其余方法声明 |
| `common/src/perf/perf_registry.cpp` | `PerfRingBuffer::Snapshot()`, `PerfRingRegistry::Init()` 实现 | 实现文件：序列化 **RingBuffer（环形缓冲区）** 内容为 **Span（跨度）** 列表、单例初始化 |
| `common/include/common/perf_logger.h` | `perf::Log()` 函数（修改，末尾追加 `Registry::Push`） | 头文件（inline）：格式化 PERF 行后调用 `PerfRingRegistry::Instance().Push(span)` |

两者关系：`perf_logger.h` 是记录入口，`perf_registry.h` 是存储后端。前者 include 后者。每个业务服务编译时链接 `common` 即可同时获得两者。`perf_registry.cpp` 的编译产物包含在 `common_lib` 静态库中。

### 3.2 各业务服务（proxy, feature, recall, precalc-and-rank-master, rank-sub）

| 位置 | 内容 | 职责 |
|------|------|------|
| `main.cpp` | 注册 `/debug/perf` **BRPC（百度 RPC 框架）** service | handler 调用 `PerfRingRegistry::Instance().Snapshot()`，返回 Protobuf 响应 |
| 进程内存 | `PerfRingRegistry` 单例（50K 条，~6MB） | 缓存 **Span（跨度）**，等待 **collector（采集器）** 拉取 |

每个业务服务**不依赖** perf-collector。collector 宕机时 **RingBuffer（环形缓冲区）** 自旋覆盖旧数据，业务服务不受影响。

### 3.3 `services/perf/collector/` — 采集 + 聚合 + 存储层

| 文件 | 职责 |
|------|------|
| `src/main.cpp` | **BRPC（百度 RPC 框架）** server 启动 + **Puller（拉取器）** 启动 |
| `src/puller.cpp` | 每秒通过 **ServiceDiscovery（服务发现）** 发现各服务，BRPC 调用 `/debug/perf` 拉取 **Span（跨度）** |
| `src/stats_engine.cpp` | `WelfordRunningStats` 类：**Welford 算法** 增量更新 avg/variance + **Reservoir Sampling（蓄水池抽样）** 估算分位数 |
| `src/sqlite_store.cpp` | 建表、批量 INSERT（1000 行/事务）、留存清理（可配置天数） |
| `src/series_manager.cpp` | 管理 **Series（请求系列）** 的 start/stop 生命周期，对 span 打 `series_id` 标签 |
| `src/api_handlers.cpp` | REST API handler（stats / trace / series / **Outlier（离群值）** / health） |

### 3.4 `services/perf/webui/` — 展示层

纯静态前端，通过 `fetch()` 调用 **collector（采集器）** 的 REST API，Chart.js 渲染图表。

## 4. 数据结构与算法

### 4.1 PerfRingBuffer

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
1. pos = write_pos_.fetch_add(1, memory_order_relaxed)
2. if (pos - read_pos_.load() >= capacity_)
       dropped_.fetch_add(1)
3. buffer_[pos % capacity_] = span
```

**Snapshot 算法**（collector BRPC handler 调用）：

```
1. w = write_pos_.load(memory_order_acquire)
2. r = read_pos_.load()
3. for i in [r, w): result.push_back(buffer_[i % capacity_])
4. read_pos_.store(w, memory_order_release)
5. return {spans: result, dropped: dropped_.exchange(0)}
```

**复杂度**：Push O(1), Snapshot O(k) where k = write_pos - read_pos。写入路径无锁、无堆分配、无系统调用。

### 4.2 WelfordRunningStats

**目的**：增量计算平均值和方差，不需要存储全部样本。

**算法**（Knuth/Welford）：

```
对每个新值 x:
    count += 1
    delta  = x - mean
    mean  += delta / count
    delta2 = x - mean
    M2    += delta * delta2

结果:
    avg    = mean
    var    = M2 / count
    stddev = sqrt(var)
```

**分位数估算**：**Reservoir Sampling（蓄水池抽样）**（1%）。维护一个固定大小（N/100）的 reservoir 数组。

### 4.3 OutlierDetector

**算法**：z-score 方法。对每个 stage 维护一个 WelfordRunningStats。

```
判定: |x - avg| > threshold × stddev → is_outlier = true
threshold 可配置，默认 3.0（3σ 规则）
```

### 4.4 SQLite Schema

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
```

**写入策略**：批量 INSERT（1000 行/事务），每 1 秒 commit 一次。WAL 模式下读写不互斥。

**清理策略**：`retention_days` 可配置，值为 0 时永久留存，否则每天清理超出天数的旧数据。

### 4.5 SeriesManager

**状态机**：

```
POST /start → status=active, start_ts=now
POST /stop  → status=stopped, stop_ts=now
     ├── 所有从 start_ts 到 stop_ts 的 span 标记 series_id
     └── stats 查询时 WHERE series_id = ? GROUP BY stage
```

## 5. 拉取协议

### 5.1 服务端（各业务 Pod）

在 `brpc::Server` 中注册一个 **BRPC（百度 RPC 框架）** service，method 为 `/debug/perf`：

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

### 5.2 客户端（perf-collector Puller）

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

服务列表通过 `common::ServiceDiscovery` 动态获取。每次拉取后立即提交 batch 到 **SQLite（嵌入式关系数据库）**。

## 6. 服务发现的集成

perf-collector 复用现有的 `common::ServiceDiscovery`（与 Proxy 相同的方式）。启动时配置 `--registry_backend=etcd --etcd_endpoints=etcd-client:2379`。

拉取的目标服务列表通过 **ServiceDiscovery（服务发现）** 获取。collector 不需要知道具体 Pod IP，ServiceDiscovery 自动做负载均衡和 failover。
