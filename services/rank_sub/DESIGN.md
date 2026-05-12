# 精排子图服务设计方案

## 1. 设计目标

RankServiceSub 是推荐系统精排层的工作节点，负责从元戎 KVWorker 读取前置计算结果（用户特征 tensor），对分配到的候选 SKU 进行打分，并将打分结果返回给 RankServiceMaster。

**核心需求**：
- 无状态设计：每个实例独立运行，不依赖本地状态
- 从 KVWorker 读取前置计算结果（8.5MB tensor）
- 对 SKU 列表进行打分（当前为模拟打分，未来替换为真实模型）
- 支持水平扩展：通过 `docker-compose scale` 快速增减实例
- 容器内端口统一（8006），通过 Docker 内部 DNS 实现负载均衡



## 2. 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                      RankServiceSub (:8006)                      │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    BRPC RPC Handler                        │  │
│  │  Rank(RankSubRequest) → RankSubResponse                    │  │
│  └──────────────────────────┬────────────────────────────────┘  │
│                              │                                   │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                  Global Thread Pool                         │  │
│  │  submit(process_rank_request)                               │  │
│  └──────────────────────────┬────────────────────────────────┘  │
│                              │                                   │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │              process_rank_request()                         │  │
│  │                                                             │  │
│  │  1. Validate request (key, skus_sub)                       │  │
│  │  2. Read user_feat from KVWorker (Get)                     │  │
│  │  3. Parse SKU IDs from skus_sub                            │  │
│  │  4. Score each SKU (simulate_score)                        │  │
│  │  5. Optional: simulate scoring delay                       │  │
│  │  6. Return skus_id[] + skus_score[]                        │  │
│  └──────────────────────────┬────────────────────────────────┘  │
│                              │                                   │
└──────────────────────────────┼───────────────────────────────────┘
                               │
          ┌────────────────────┼────────────────────┐
          │                    │                    │
          │ KVWorker Read      │                    │
          ▼                    │                    │
┌───────────────────────┐     │                    │
│  元戎 KVWorker Cluster │     │                    │
│  (:31502)              │     │                    │
│                        │     │                    │
│  Get("123456")         │     │                    │
│  → 8.5MB tensor data   │     │                    │
└───────────────────────┘     │                    │
                               │                    │
                               ▼                    ▼
                    ┌──────────────────┐  ┌──────────────────┐
                    │  RankSub #0      │  │  RankSub #N      │
                    │  (same image)    │  │  (same image)    │
                    └──────────────────┘  └──────────────────┘
```

## 3. 目录结构

```
services/rank_service_sub/
├── DESIGN.md                    # 本文档
├── README.md                    # 模块介绍与使用说明
├── CMakeLists.txt               # CMake 构建配置
├── build.sh                     # 编译脚本
├── server/
│   ├── include/
│   │   └── rank_sub_server.h    # RankSubServiceImpl 声明
│   └── src/
│       ├── main.cpp             # 服务入口
│       └── rank_sub_server.cpp  # 服务实现
└── client/
    └── rank_sub_client.cpp      # 测试客户端
├── tests/
│   └── test_rank_sub.cpp        # 单元测试
└── utils/                       # 工具目录
```

## 4. Protobuf 协议定义

**文件**：`proto/rank_sub.proto`

```protobuf
syntax = "proto3";
package rank;
option cc_generic_services = true;

message RankSubRequest {
    string user_feat_key = 1;  // KVWorker 中的用户特征键
    string skus_sub = 2;       // 分配给本子图的 SKU 列表
    string payload = 3;        // 模拟负载
    string trace_id = 4;       // 分布式追踪 ID
}

message RankSubResponse {
    repeated uint32 skus_id = 1;     // SKU ID 列表
    repeated uint64 skus_score = 2;  // 打分结果（score × 100）
}

service RankSubService {
    rpc Rank(RankSubRequest) returns (RankSubResponse);
}
```

**打分编码**：`skus_score` 存储的是 `score × 100` 的整数值，避免浮点数传输精度问题。例如 `score=0.85` → `skus_score=85`。

## 5. 组件详述

### 6.1 RankSubServiceImpl

核心服务类，处理精排子图请求。

**请求处理流程**：

1. 提取 `trace_id` 并注入日志系统（分布式追踪）
2. 通过全局线程池异步执行处理任务
3. 验证 `user_feat_key` 和 `skus_sub` 非空
4. 从 KVWorker 读取前置计算结果：`KVClient::Get(key, buffer)`
5. 解析 SKU ID 列表
6. 对每个 SKU 调用 `simulate_score()` 打分
7. 可选：模拟打分耗时（`scoring_delay_ms`）
8. 返回 `skus_id[]` 和 `skus_score[]`
9. 若启用 `enable_timing_stats`，记录 `kv_read_cost`、`scoring_cost`、`simulated_delay`、`server_process_total` 耗时

### 6.2 KVWorker 读取流程

```
RankSubService                  KVWorker
     │                            │
     │  ConnectOptions            │
     │  host: 141.61.84.245       │
     │  port: 31502               │
     │                            │
     │  Init()                    │
     │───────────────────────────▶│
     │◀─────── OK ───────────────│
     │                            │
     │  Get(key, buffer)          │
     │───────────────────────────▶│
     │◀─── Buffer (8.5MB) ───────│
     │                            │
     │  buffer->ImmutableData()   │
     │  buffer->GetSize()         │
     │  → user_feat string        │
```

**关键设计**：
- 使用 `Buffer` 方式获取大数据（8.5MB），避免拷贝
- `ImmutableData()` 返回只读指针，`GetSize()` 返回数据大小
- 每次请求创建独立的 `KVClient` 实例（线程安全）

### 6.3 模拟打分模型

当前使用哈希函数生成模拟分数：

```cpp
double simulate_score(uint64_t sku_id, const std::string& user_feat) {
    std::hash<std::string> hasher;
    size_t user_hash = hasher(user_feat);
    size_t sku_hash = std::hash<uint64_t>{}(sku_id);
    double score = static_cast<double>((user_hash ^ sku_hash) % 10000) / 100.0;
    return score;  // 0.00 ~ 99.99
}
```

**特性**：
- 确定性：相同 (sku_id, user_feat) 始终产生相同分数
- 均匀分布：分数在 0-100 范围内大致均匀
- 可替换：未来替换为真实模型推理，接口不变

### 6.4 水平扩展方案

```
                    Docker Network (lingquickrec)
                    ┌───────────────────────────────┐
                    │                               │
   ┌───────────────┼───────────────────────────────┼───────────┐
   │               │                               │           │
   │    ┌──────────▼──────────┐    ┌───────────────▼────────┐  │
   │    │   RankSub #0        │    │   RankSub #1           │  │
   │    │   rank-sub-service  │    │   rank-sub-service     │  │
   │    │   :8006             │    │   :8006                │  │
   │    └─────────────────────┘    └────────────────────────┘  │
   │                                                            │
   │    ┌─────────────────────┐    ┌────────────────────────┐  │
   │    │   RankSub #N-1      │    │   RankSub #N           │  │
   │    │   rank-sub-service  │    │   rank-sub-service     │  │
   │    │   :8006             │    │   :8006                │  │
   │    └─────────────────────┘    └────────────────────────┘  │
   │                                                            │
   │               ▲ DNS Round Robin ▲                         │
   │               │                 │                          │
   │    ┌──────────┴─────────────────┴──────────────────────┐  │
   │    │            RankMaster (:8005)                      │  │
   │    │            Channel → rank-sub-service:8006         │  │
   │    └───────────────────────────────────────────────────┘  │
   └────────────────────────────────────────────────────────────┘
```

**扩展命令**：
```bash
# 扩容到 20 个实例
docker-compose up -d --scale rank-sub-service=20

# 缩容到 5 个实例
docker-compose up -d --scale rank-sub-service=5
```

**无状态保证**：
- 每个实例完全独立，无共享状态
- 所有数据从 KVWorker 读取
- 实例增减不影响其他实例
- Docker DNS 自动更新实例列表

## 6. 容器集成方案

### 7.1 Dockerfile

基于 `brpc_base:v1.2`，元戎 SDK 已包含在基础镜像中。

### 7.2 关键配置

- **不设 container_name**：scale 时多个容器不能同名
- **端口范围映射**：`8006-8015:8006`（宿主机访问用）
- **同一网络**：所有实例加入 `lingquickrec` 网络

### 7.3 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SERVER_PORT` | 8006 | 服务端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 主机 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `SCORING_DELAY_MS` | 100 | 模拟打分延迟 |

## 7. 配置参数总表

| 参数名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `--server_port` | int32 | 8006 | 服务监听端口 |
| `--kvworker_host` | string | "141.61.84.245" | KVWorker 主机地址 |
| `--kvworker_port` | int32 | 31502 | KVWorker 端口 |
| `--etcd_address` | string | "141.61.84.245:2379" | ETCD 地址 |
| `--scoring_delay_ms` | int32 | 100 | 模拟打分耗时（毫秒） |
| `--enable_timing_stats` | bool | true | 是否启用详细时延统计 |

## 8. 端口分配

| 服务 | 端口 | 说明 |
|------|------|------|
| RankServiceSub | 8006 | BRPC 服务端口（所有实例统一） |
| KVWorker (Rank) | 31502 | 元戎 KVWorker 端口 |

## 9. 数据流

```
RankMaster              RankSub (:8006)               KVWorker
     │                       │                           │
     │  RankSubRequest       │                           │
     │  (key, skus_sub)      │                           │
     │──────────────────────▶│                           │
     │                       │                           │
     │                       │  1. KVClient::Get(key)    │
     │                       │──────────────────────────▶│
     │                       │◀── Buffer (8.5MB) ───────│
     │                       │                           │
     │                       │  2. Parse SKUs            │
     │                       │     [100456, 200789, ...] │
     │                       │                           │
     │                       │  3. Score each SKU        │
     │                       │     simulate_score()      │
     │                       │     → {100456: 0.85,      │
     │                       │        200789: 0.72, ...} │
     │                       │                           │
     │                       │  4. Optional: delay       │
     │                       │     sleep(scoring_delay)  │
     │                       │                           │
     │  RankSubResponse      │                           │
     │  (skus_id: [...],     │                           │
     │   skus_score: [...])  │                           │
     │◀──────────────────────│                           │
```

## 10. 错误处理与边界情况

| 场景 | 行为 |
|------|------|
| **空 user_feat_key** | 返回 `EMPTY_USER_FEAT_KEY` 错误，记录 ERROR |
| **空 skus_sub** | 返回 `EMPTY_SKUS_SUB` 错误，记录 ERROR |
| **KVWorker Init 失败** | 返回 `KVCLIENT_INIT_FAILED` 错误，记录 ERROR |
| **KVWorker Get 失败** | 返回 `KVCLIENT_GET_FAILED` 错误，记录 ERROR（含 key） |
| **key 不存在（TTL 过期）** | KVWorker 返回错误，记录 ERROR |
| **SKU 解析失败** | 返回 `NO_SKU_PARSED` 错误，记录 ERROR |
| **KVWorker 不可达** | 所有请求失败，需检查网络 |
| **实例被 kill** | Docker 自动重启（restart: unless-stopped） |
| **打分延迟过大** | Master 侧 `sub_worker_timeout_ms` 超时兜底 |
| **线程池任务异常** | 返回 `INTERNAL_ERROR` 错误，记录 ERROR |

## 11. 演进规划

| 版本 | 特性 |
|------|------|
| **V1.0** | 核心功能：KVWorker 读取 + 模拟打分 + 水平扩展 |
| **V1.1** | 真实打分：替换 simulate_score 为模型推理 |
| **V1.2** | KVWorker 连接池：复用 KVClient 实例，减少 Init 开销 |
| **V1.3** | 批量打分：支持 GPU 批量推理，提升吞吐 |
| **V2.0** | 本地缓存：热点 user_feat 本地缓存，减少 KVWorker 读取 |
