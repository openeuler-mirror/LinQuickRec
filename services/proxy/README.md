# Proxy 服务（网关）

## 模块简介

Proxy 是 LingQuickRec 系统的**网关入口**，对外暴露 HTTP 接口，对内持有 FeatureService、RecallService、PrecalcService、RankServiceMaster 四个下游服务的连接。它负责接收外部推荐请求，按业务编排顺序依次调用下游服务，最终返回排序后的候选结果。

Proxy 通过 [discovery 服务](../discovery/) 动态获取下游实例，不配置静态地址。每个请求向 discovery server 发起 `Discover()` RPC 获取 UP 实例列表，按 round-robin 选取，失败时自动重试下一实例。连续 3 次失败触发熔断（10s cooldown）。所有下游调用携带 trace_id 以支持全链路追踪。

### 可观测性

**阶段时延统计**（`--enable_timing_stats=true`）：

```
[Proxy Timing]  feature=12.34ms recall+precalc=67.89ms rank=234.56ms total=314.79ms
```

**日志**：使用 `common::logger`（项目统一日志系统），trace_id 自动附加到每行日志。

## 目录结构

```
services/proxy/
├── CMakeLists.txt                     # 构建配置
├── Dockerfile                         # 容器镜像
├── README.md                          # 本文档
├── DESIGN.md                          # 详细设计文档
├── server/
│   ├── include/
│   │   ├── proxy_server.h             # ProxyServiceImpl 类声明
│   │   └── service_discovery.h        # 服务发现客户端
│   └── src/
│       ├── main.cpp                   # 服务入口
│       ├── proxy_server.cpp           # 核心编排逻辑
│       └── service_discovery.cpp      # 服务发现实现
├── client/
│   └── proxy_test_client.cpp          # 手动测试客户端
└── tests/
    └── integration_test.cpp           # 单进程集成测试
```

## 业务流程

```
  External HTTP Request
         │
         ▼
   ┌───────────────────────────────────────────────────────────────────┐
   │  Stage 1: Get Features (sync)                                    │
   │  ┌────────────────────────────────────────────────────────────┐   │
   │  │  POST -> FeatureService -> GetUserFeatures                 │   │
   │  └────────────────────────────────────────────────────────────┘   │
   └───────────────────────────────────────────────────────────────────┘
         │
         ▼
   ┌───────────────────────────────────────────────────────────────────┐
   │  Stage 2: Recall & Precalc (parallel, global thread pool)        │
   │  ┌─────────────────────────────┐  ┌───────────────────────────┐  │
   │  │  POST -> RecallService      │  │  POST -> PrecalcService   │  │
   │  │  -> Recall(sku_ids)         │  │  -> Precalculate(key)     │  │
   │  └─────────────────────────────┘  └───────────────────────────┘  │
   └───────────────────────────────────────────────────────────────────┘
         │
         ▼
   ┌───────────────────────────────────────────────────────────────────┐
   │  Stage 3: Rank (sync)                                            │
   │  ┌────────────────────────────────────────────────────────────┐   │
   │  │  POST -> RankServiceMaster -> Rank                         │   │
   │  │  Input: user_feat_key + skus(candidates)                   │   │
   │  │  Output: sorted candidates[]                               │   │
   │  └────────────────────────────────────────────────────────────┘   │
   └───────────────────────────────────────────────────────────────────┘
         │
         ▼
   Return HTTP Response (with error_code / error_message)
```

### 阶段时序

| 阶段 | 调用方式 | 依赖服务 | 说明 |
|------|---------|---------|------|
| Stage 1: 特征获取 | **同步阻塞** | FeatureService | 必须拿到用户特征后才能进行后续操作 |
| Stage 2a: 召回 | **异步并行**（全局线程池） | RecallService | 与 Stage 2b 同时发起，互不依赖 |
| Stage 2b: 预计算 | **异步并行**（全局线程池） | PrecalcService | 与 Stage 2a 同时发起，互不依赖 |
| Stage 3: 精排 | **同步阻塞** | RankServiceMaster | 必须等 Stage 2a/2b 都完成后才能执行 |

### API 接口

```
POST /Proxy/Recommend
Content-Type: application/json
```

**请求体：**

```json
{
  "user_id": 12345,
  "payload": "optional附加数据"
}
```

**响应体（成功）：**

```json
{
  "candidates": [100001, 100002, 100003],
  "error_code": 0,
  "error_message": ""
}
```

**响应体（失败）：**

```json
{
  "candidates": [],
  "error_code": 16973825,
  "error_message": "FeatureService: connection refused"
}
```

### error_code 编码

格式：`0xMMTTCCCC`

| 字节 | 含义 | 示例 |
|------|------|------|
| MM | 模块 (PROXY=0x01) | 0x01 |
| TT | 类型 (SERVICE_ERROR=0x03) | 0x03 |
| CCCC | 具体错误码 | 0x0001~0x0004 |

| error_code | 含义 |
|-----------|------|
| 0x01030001 | FeatureService 调用失败 |
| 0x01030002 | RecallService 调用失败 |
| 0x01030003 | PrecalcService 调用失败 |
| 0x01030004 | RankServiceMaster 调用失败 |

### 错误处理

| 场景 | error_code | 行为 |
|------|-----------|------|
| Feature 调用失败/超时 | 0x01030001 | 终止请求，不执行后续阶段 |
| Recall 调用失败/超时 | 0x01030002 | 终止请求（Rank 同时依赖 Recall + Precalc 的结果） |
| Precalc 调用失败/超时 | 0x01030003 | 终止请求 |
| Rank 调用失败/超时 | 0x01030004 | 终止请求，无候选结果 |
| 全部成功 | 0 | 正常返回 candidates |

### trace_id

每个请求在入口生成 trace_id：**32 字符 hex** = 前 16 字符微秒时间戳 + 后 16 字符随机数。

```text
a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6
├── timestamp ─┤├── random ──────┤
```

trace_id 通过 `cntl.set_log_id()` 传递到所有下游 RPC，下游服务可通过 `controller->log_id()` 获取。

## 编译命令

### 前置依赖

| 依赖 | 版本要求 | 安装参考 |
|------|----------|----------|
| CMake | >= 3.14 | `apt install cmake` / `brew install cmake` |
| brpc | latest | [brpc 构建指南](https://github.com/apache/brpc/blob/master/docs/cn/getting_started.md) |
| protobuf | >= 3.x | brpc 自带或单独安装 |
| abseil-cpp | latest | brpc 自带或单独安装 |
| gflags | latest | `apt install libgflags-dev` |
| pthread | 系统自带 | — |

### 编译

```bash
# 在项目根目录
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make proxy_server proxy_test_client proxy_integration_test -j$(nproc)
```

产物在 `build/bin/` 下：

| 二进制 | 说明 |
|--------|------|
| `proxy_server` | 服务端 |
| `proxy_test_client` | 手动测试客户端 |
| `proxy_integration_test` | 单进程集成测试 |

## 启动方式

### 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| **服务发现** | | |
| `--discovery_addr` | "127.0.0.1:8100" | Discovery server 地址 |
| `--discovery_refresh_interval_ms` | 5000 | 缓存刷新间隔 (ms) |
| `--downstream_max_retries` | 2 | 每个下游最大重试次数 |
| **下游服务名** | | |
| `--feature_service_name` | "feature_service" | Feature 服务注册名 |
| `--recall_service_name` | "recall_service" | Recall 服务注册名 |
| `--precalc_service_name` | "precalc_service" | Precalc 服务注册名 |
| `--rank_service_name` | "rank_service" | Rank 服务注册名 |
| **超时** | | |
| `--feature_timeout_ms` | 3000 | Feature 调用超时 (ms) |
| `--recall_timeout_ms` | 5000 | Recall 调用超时 (ms) |
| `--precalc_timeout_ms` | 5000 | Precalc 调用超时 (ms) |
| `--rank_timeout_ms` | 10000 | Rank 调用超时 (ms) |
| **其他** | | |
| `--server_port` | 8080 | Proxy HTTP 服务监听端口 |
| `--enable_timing_stats` | true | 是否打印阶段时延统计 |
| `--global_thread_pool_size` | 128 (auto) | 全局线程池大小，默认自动根据 CPU 核数计算 |

### 直接启动

```bash
./build/bin/proxy_server \
    --server_port=8080 \
    --discovery_addr="discovery-server:8100" \
    --enable_timing_stats=true
```

## 容器搭建

### 构建镜像

```bash
# 在项目根目录下执行
docker build -t lingquickrec/proxy:latest -f services/proxy/Dockerfile .
```

### 运行

```bash
# 单容器运行（依赖外部 discovery server）
docker run -p 8080:8080 \
    lingquickrec/proxy:latest \
    --discovery_addr="discovery-server:8100"

# 集成测试（镜像已内置 mock 服务时使用）
docker run --rm lingquickrec/proxy:latest \
    ./build/bin/proxy_integration_test

## 测试方法

### 集成测试

单进程集成测试，无需外部依赖：

```bash
cd build && cmake .. && make proxy_integration_test
./bin/proxy_integration_test
```

覆盖 5 个场景：

| 场景 | 验证内容 |
|------|----------|
| 全链路成功 | 3 个 candidates 按正确顺序返回 |
| Feature 服务失败 | 返回 error_code = 0x01030001 |
| Recall 服务失败 | 返回 error_code = 0x01030002 |
| Precalc 服务失败 | 返回 error_code = 0x01030003 |
| Rank 服务失败 | 返回 error_code = 0x01030004 |

### 手动测试

需要 proxy 运行中且 discovery 上已注册下游服务：

```bash
./build/bin/proxy_test_client \
    --server="127.0.0.1:8080" \
    --user_id=12345
```

输出示例：

```
Proxy Test Client starting...
Connecting to Proxy at: 127.0.0.1:8080
Sending Recommend request: user_id=12345 payload=test_request
Recommend response received: candidates=3 latency=123.45ms
```
```
