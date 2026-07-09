# Perf — 性能可观测性系统

## 模块简介

Perf 是 LinQuickRec 的实时性能监控服务，负责采集、聚合、持久化、可视化全链路性能数据。它由两部分组成：

1. 嵌入各业务服务的轻量 RingBuffer（`common/perf_registry.h`），通过 CAS 原子操作零阻塞记录 span；
2. 独立的 `perf-collector` 服务，定时拉取各服务 span、增量计算统计指标、持久化到 SQLite、并通过 REST API + WebUI 对外暴露。

Perf 通过 etcd 或 discovery_server 动态发现下游服务（proxy、feature、recall 等），每秒通过 BRPC 调用各服务的 `/debug/perf` RPC 拉取 span 数据。collector 宕机时业务服务不受影响（ring buffer 自旋），恢复后自动补拉数据。

### 可观测性

Perf collector 自身暴露可供拉取数据的 HTTP 端口，可通过 Prometheus/Grafana 监控。

### RESTful API

| Method | Path | 说明 |
|--------|------|------|
| `GET` | `/api/v1/health` | 健康检查 |
| `GET` | `/api/v1/metrics?window=1m` | collector 自身指标 |
| `GET` | `/api/v1/stats/current?stage=proxy_e2e` | 当前实时统计 (avg/p50/p99/...) |
| `GET` | `/api/v1/stats/history?stage=proxy_e2e&from=...&to=...` | 历史统计 (按 10s 窗口聚合) |
| `GET` | `/api/v1/stats/compare?stages=proxy_e2e,recall_total&window=5m` | 多 stage 对比 |
| `GET` | `/api/v1/trace/{trace_id}` | 按 trace_id 查询完整请求链路 |
| `POST` | `/api/v1/series/start?name=baseline` | 开始新请求系列 |
| `POST` | `/api/v1/series/stop` | 停止当前系列 |
| `GET` | `/api/v1/series` | 列出所有系列 |
| `GET` | `/api/v1/series/{id}/stats` | 某系列各 stage 统计 |
| `DELETE` | `/api/v1/series/{id}` | 删除系列 |

## 目录结构

```
services/perf/
├── CMakeLists.txt
├── DESIGN.md
├── README.md
├── TODO.md
├── collector/
│   ├── CMakeLists.txt
│   ├── Dockerfile
│   ├── entrypoint.sh
│   └── src/
│       ├── main.cpp                  # BRPC server + puller startup
│       ├── puller.cpp                # Periodic BRPC puller via discovery
│       ├── stats_engine.cpp          # Welford incremental statistics
│       ├── sqlite_store.cpp          # SQLite batch persistence
│       ├── outlier_detector.cpp      # z-score outlier detection
│       ├── series_manager.cpp        # Series lifecycle API
│       └── api_handlers.cpp          # REST API handlers
├── webui/
│   ├── index.html                    # Dashboard
│   ├── trace.html                    # Trace waterfall
│   ├── compare.html                  # A/B comparison
│   └── js/
│       ├── api.js
│       ├── dashboard.js
│       └── trace.js
└── tests/
    └── integration_test.cpp          # Integration test with mock services

common/include/common/
├── perf_registry.h                   # PerfRingBuffer + PerfRingRegistry (declaration, Push inline)
└── perf_logger.h                     # perf::Log() (modified: calls Registry::Push)

common/src/perf/
└── perf_registry.cpp                 # Snapshot(), Init() implementation
```

## 业务流程

```
 Client Request
       │
       v
┌────────────────────────────────── proxy ──────────────────────────────────────┐
│                                                                               │
│  Feature -> (Recall + Precalc) -> RankMaster -> RankSub -> Response           │
│                                                                               │
│  Each stage: perf::Log() -> RingBuffer(CAS)                                   │
│                                                                               │
└─────────────────────────────────────┬─────────────────────────────────────────┘
                                      │  /debug/perf (BRPC pull, 1s)
                                      v
┌──────────────────────── perf-collector ────────────────────────────────────────┐
│                                                                                │
│  ┌──────────┐    ┌───────────────┐    ┌──────────────┐    ┌──────────────────┐ │
│  │  Puller  │ -> │  StatsEngine  │ -> │  SqliteStore │ -> │  API Handlers    │ │
│  │ (1s loop)│    │   (Welford)   │    │  (WAL mode)  │    │  (REST + HTML)   │ │
│  └──────────┘    └───────┬───────┘    └──────────────┘    └────────┬─────────┘ │
│                          │                                          │          │
│                   ┌──────┴──────┐                          ┌───────┴──────┐    │
│                   │SeriesManager│                          │    WebUI     │    │
│                   │ (start/stop)│                          │  (Chart.js)  │    │
│                   └──────┬──────┘                          └──────────────┘    │
│                          │                                                     │
│                   ┌──────┴──────┐                                              │
│                   │  Outlier    │                                              │
│                   │  Detector   │                                              │
│                   │  (z-score)  │                                              │
│                   └─────────────┘                                              │
└────────────────────────────────────────────────────────────────────────────────┘
```

## 编译命令

| 依赖 | 版本要求 | 备注 |
|------|----------|------|
| CMake | >= 3.14 | 编译工具链 |
| brpc | >= 1.4 | `linquickrec/base:latest` 基础镜像已内置 |
| protobuf | >= 3.0 | `linquickrec/base:latest` 基础镜像已内置 |
| abseil-cpp | latest | `linquickrec/base:latest` 基础镜像已内置 |
| sqlite3 | >= 3.25 | WAL mode and UPSERT support |

### 脚本构建

```bash
bash build.sh              # Release 构建
bash build.sh debug        # Debug 构建
```

### 手动构建

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make perf_collector perf_integration_test -j$(nproc)
```

### 编译产物

| 二进制 | 说明 |
|--------|------|
| `perf_collector` | perf-collector 服务端 (HTTP + puller + stats + SQLite) |
| `perf_integration_test` | 单进程集成测试 |

## 启动方式

### 启动命令

```bash
./build/bin/perf_collector \
    --server_port=8080 \
    --registry_backend=etcd \
    --etcd_endpoints=etcd-client:2379 \
    --retention_days=30
```

### 配置参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **服务发现** | | | |
| `--registry_backend` | string | "etcd" | 注册中心后端（etcd / discovery_server） |
| `--etcd_endpoints` | string | "etcd-client:2379" | etcd 端点地址 |
| `--discovery_addr` | string | "127.0.0.1:8100" | Discovery server 地址 |
| **采集** | | | |
| `--pull_interval_ms` | int32 | 1000 | 拉取间隔 (ms) |
| `--ring_buffer_size` | int32 | 50000 | RingBuffer 容量（条） |
| **存储** | | | |
| `--retention_days` | int32 | 30 | 数据留存天数 |
| `--sqlite_db_path` | string | "/var/lib/perf/perf.db" | SQLite 数据库路径 |
| **离群值** | | | |
| `--outlier_threshold` | double | 3.0 | z-score 阈值 |
| **其他** | | | |
| `--server_port` | int32 | 8080 | HTTP 服务监听端口 |
| `--server_num_threads` | int32 | 0 | bthread 线程数，0=BRPC 默认 |

## 容器搭建

### 构建镜像

```bash
# 在项目根目录下执行
docker build -t linquickrec/perf-collector:latest \
  -f services/perf/collector/Dockerfile .
```

### 运行容器

```bash
# etcd 模式（默认）
docker run -d --name perf-collector \
  -p 32000:8080 \
  -e REGISTRY_BACKEND=etcd \
  -e ETCD_ENDPOINTS=<etcd-ip>:2379 \
  -v perf-data:/var/lib/perf \
  linquickrec/perf-collector:latest

# discovery_server 模式
docker run -d --name perf-collector \
  -p 32000:8080 \
  -e REGISTRY_BACKEND=discovery_server \
  -e DISCOVERY_ADDR=<discovery-server-ip>:8100 \
  -v perf-data:/var/lib/perf \
  linquickrec/perf-collector:latest
```

## WebUI 访问方式

Perf-collector 的 K8s Service 通过 NodePort 32000 暴露，可直接在浏览器中访问：

```
http://<任一节点IP>:32000
```

### Dashboard
- 下拉选择监控的 stage（proxy_e2e、recall_total 等）
- 实时显示 count / avg / p50 / p99 四个指标卡片 + 柱状图
- 每 3 秒自动刷新

### Series（请求系列）
- **Start**：点击 + New Series 开始记录一个请求系列
- **Stop**：点击 Stop Active 停止当前系列
- 表格列出所有系列（名称、状态、span 数量）
- 点击系列名查看统计值

### Trace（链路追踪）
- 输入 trace_id → 搜索
- 瀑布图展示该请求跨所有服务的 Span 耗时分布
- 每个 Span 显示 service / stage / duration（数值 + 横向柱）

## 测试方法

### 集成测试

集成测试为单进程测试，不依赖 etcd 或 K8s 集群。测试在一个进程内启动 mock `/debug/perf` HTTP server + 真实 `perf-collector`（Puller + StatsEngine + SqliteStore），注入已知分布的 span 数据并用 `assert()` 验证各 API 返回结果。

```bash
# 本地运行
./build/bin/perf_integration_test

# 通过 Docker 运行
docker run --rm linquickrec/perf-collector:latest test
```

覆盖 4 个场景：

| 场景 | 验证内容 |
|------|----------|
| 基本统计 | 注入 100 条 span (avg=50, stddev=5)，验证 avg=47~53, count=100 |
| 分位数 | 验证 p99 在合理范围内 |
| 离群值检测 | 注入 99 条正常 + 1 条异常 (10× avg)，验证被标记 |
| 系列管理 | start → 注入 50 条 → stop → 验证 series 统计 count=50 |

### 手动测试

```bash
# 创建请求系列
curl -X POST "http://127.0.0.1:32000/api/v1/series/start?name=baseline"

# 发一批请求到 proxy
bash scripts/send_proxy_request.sh --k8s -n 100

# 等 3s 让 collector 拉取
sleep 3

# 停止系列
curl -X POST "http://127.0.0.1:32000/api/v1/series/stop"

# 查看系列统计
curl "http://127.0.0.1:32000/api/v1/series/1/stats" | jq '.proxy_e2e'
# 预期: {"avg": ..., "p99": ..., "count": 100}
```

## 端口对照表

| 端口 | 服务 | 协议 | 说明 |
|------|------|------|------|
| 32000 | perf-collector | HTTP | REST API + WebUI |
