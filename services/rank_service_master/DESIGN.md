# 精排主图服务设计方案

## 1. 设计目标

RankServiceMaster 是推荐系统精排层的主控节点，负责将召回的候选商品集分发到多个 RankSub 子图进行并行打分，然后归并所有子图结果选出 Top-K 商品。

**核心需求**：

- 接收 `user_feat_key` 和候选 SKU 列表，将 SKU 哈希分片到多个子图
- 并行调用所有 RankSub 子图，收集打分结果
- 从所有子图结果中归并选出 Top-K 商品
- 支持动态扩缩容：RankSub 实例数量可通过 docker-compose scale 调整
- Channel 池复用：预创建到每个 RankSub 的连接池，避免频繁建连

## 2. 系统架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      RankServiceMaster (:8005)                          │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                      BRPC RPC Handler                             │  │
│  │  Rank(RankMasterRequest) → RankMasterResponse                     │  │
│  └──────────────────────────┬────────────────────────────────────────┘  │
│                              │                                           │
│                              ▼                                           │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                    process_rank_request()                          │  │
│  │                                                                    │  │
│  │  1. Parse SKU IDs from skus string (6 digits each)                │  │
│  │  2. Distribute SKUs by hash → {worker_0: [...], worker_1: ...}   │  │
│  │  3. Parallel call all sub-workers (std::async)                    │  │
│  │  4. Collect scores from all sub-workers                           │  │
│  │  5. Select Top-K from merged scores                               │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                    Channel Pool (pre-connected)                    │  │
│  │  channel[0] ──▶ rank-sub-service:8006 (instance 1)                │  │
│  │  channel[1] ──▶ rank-sub-service:8006 (instance 2)                │  │
│  │  channel[2] ──▶ rank-sub-service:8006 (instance 3)                │  │
│  │  ...                                                               │  │
│  │  channel[N] ──▶ rank-sub-service:8006 (instance N)                │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
         │                    │                    │
         │ RPC: Rank()        │ RPC: Rank()        │ RPC: Rank()
         ▼                    ▼                    ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  RankSub #0  │    │  RankSub #1  │    │  RankSub #N  │
│  (:8006)     │    │  (:8006)     │    │  (:8006)     │
│              │    │              │    │              │
│  1. Read     │    │  1. Read     │    │  1. Read     │
│     KVWorker │    │     KVWorker │    │     KVWorker │
│  2. Score    │    │  2. Score    │    │  2. Score    │
│     SKUs     │    │     SKUs     │    │     SKUs     │
│  3. Return   │    │  3. Return   │    │  3. Return   │
│     scores   │    │     scores   │    │     scores   │
└──────────────┘    └──────────────┘    └──────────────┘
```

## 3. 目录结构

```
services/rank_service_master/
├── DESIGN.md                    # 本文档
├── README.md                    # 模块介绍与使用说明
├── CMakeLists.txt               # CMake 构建配置
├── build.sh                     # 编译脚本
├── server/
│   ├── include/
│   │   └── rank_master_server.h # RankMasterServiceImpl 声明
│   └── src/
│       ├── main.cpp             # 服务入口
│       └── rank_master_server.cpp # 服务实现
└── client/
    └── rank_master_test_client.cpp # 测试客户端
```

## 4. Protobuf 协议定义

**文件**：`proto/rank_master.proto`

```protobuf
syntax = "proto3";
package rank;
option cc_generic_services = true;

message RankMasterRequest {
    string user_feat_key = 1;  // KVWorker 中的用户特征键
    string skus = 2;           // 候选 SKU 列表（6位数字拼接，~100KB）
    string payload = 3;        // 模拟负载（~100KB）
}

message RankMasterResponse {
    repeated uint64 candidates = 1;  // Top-K 候选商品 ID
}

service RankMasterService {
    rpc Rank(RankMasterRequest) returns (RankMasterResponse);
}
```

## 5. 组件详述

### 6.1 SKU 解析与分片

**解析规则**：SKU ID 以 6 位数字连续存储，按 6 字符步长切分：

```
skus = "123456789012111213"
        │─────│─────│─────│
         123456  789012  111213
         SKU #0  SKU #1  SKU #2
```

**哈希分片策略**：

```cpp
size_t hash = std::hash<uint64_t>{}(sku_id);
int worker_index = hash % n_workers;
```

- 使用 `std::hash<uint64_t>` 对 SKU ID 哈希
- 取模分配到 `[0, n_workers)` 的子图
- 保证同一 SKU 始终分配到同一子图（确定性分片）
- 各子图 SKU 数量大致均匀

### 6.2 并行调用（Scatter）

使用 `std::async(std::launch::async, ...)` 并行调用所有子图：

```
                    ┌─────────────┐
                    │   Master    │
                    └──────┬──────┘
           ┌───────────────┼───────────────┐
           │               │               │
    ┌──────▼──────┐ ┌──────▼──────┐ ┌──────▼──────┐
    │  async #0   │ │  async #1   │ │  async #N   │
    │  call_sub() │ │  call_sub() │ │  call_sub() │
    └──────┬──────┘ └──────┬──────┘ └──────┬──────┘
           │               │               │
           ▼               ▼               ▼
    ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
    │  RankSub #0  │ │  RankSub #1  │ │  RankSub #N  │
    └──────────────┘ └──────────────┘ └──────────────┘
```

**关键设计**：

- 跳过空分片（无 SKU 的子图不调用）
- 每个子图调用独立，互不阻塞
- 使用 `future.get()` 收集结果

### 6.3 Top-K 归并（Gather）

从所有子图收集 `{sku_id: score}` 映射后，选出 Top-K：

```
Sub #0: {SKU_1: 0.85, SKU_3: 0.72, ...}
Sub #1: {SKU_2: 0.91, SKU_4: 0.68, ...}
Sub #N: {SKU_5: 0.77, SKU_6: 0.55, ...}
              │
              ▼ Merge
{SKU_1: 0.85, SKU_2: 0.91, SKU_3: 0.72, SKU_4: 0.68, SKU_5: 0.77, SKU_6: 0.55}
              │
              ▼ Top-K (K=3)
[SKU_2(0.91), SKU_1(0.85), SKU_5(0.77)]
```

**算法选择**：

- 若 `score_count <= top_k`：全排序
- 若 `score_count > top_k`：`std::nth_element` 部分排序 + 前 K 个排序
- 时间复杂度：O(N) 部分排序 + O(K log K) 最终排序

### 6.4 Channel 池

在构造函数中预创建到所有 RankSub 实例的 Channel：

```cpp
for (int i = 0; i < sub_worker_count; ++i) {
    const std::string& addr = addresses[i % addresses.size()];
    auto channel = std::make_unique<brpc::Channel>();
    brpc::ChannelOptions opts;
    opts.timeout_ms = 5000;
    opts.connection_type = "pooled";  // 连接池复用
    channel->Init(addr.c_str(), &opts);
    sub_worker_channels_.push_back(std::move(channel));
}
```

**Docker Compose 场景**：

- `sub_worker_addresses = "rank-sub-service:8006"`
- Docker 内部 DNS 轮询解析 `rank-sub-service` 到不同容器
- 所有 Channel 初始化时连接到同一服务名，DNS 自动负载均衡

### 6.5 动态扩缩容

```
                    Scale Out
                       │
    ┌──────────────────▼──────────────────┐
    │  docker-compose scale rank-sub=20   │
    └──────────────────┬──────────────────┘
                       │
    ┌──────────────────▼──────────────────┐
    │  新增 10 个 RankSub 容器             │
    │  DNS 自动注册新实例                  │
    └──────────────────┬──────────────────┘
                       │
    ┌──────────────────▼──────────────────┐
    │  重启 RankMaster（更新 worker_count）│
    │  Channel 池重新初始化               │
    └─────────────────────────────────────┘
```

## 6. 容器集成方案

### 7.1 Dockerfile

基于 `brpc_base:v1.3`，无特殊依赖。

### 7.2 启动顺序

1. 先启动 RankSub（`docker-compose up -d --scale rank-sub-service=10 rank-sub-service`）
2. 等待 RankSub 就绪
3. 启动 RankMaster（`depends_on: rank-sub-service`）

### 7.3 环境变量

| 变量                     | 默认值                     | 说明           |
| ---------------------- | ----------------------- | ------------ |
| `SERVER_PORT`          | 8005                    | 服务端口         |
| `SUB_WORKER_COUNT`     | 10                      | 子图数量         |
| `SUB_WORKER_ADDRESSES` | "rank-sub-service:8006" | 子图地址         |
| `RANK_SUB_HOST`        | "rank-sub-service"      | 子图主机名（等待就绪用） |
| `RANK_SUB_PORT`        | 8006                    | 子图端口（等待就绪用）  |
| `TOP_K`                | 100                     | 返回前 K 个商品    |

## 7. 配置参数总表

| 参数名                      | 类型     | 默认值              | 说明           |
| ------------------------ | ------ | ---------------- | ------------ |
| `--server_port`          | int32  | 8005             | 服务监听端口       |
| `--sub_worker_count`     | int32  | 10               | 子图数量         |
| `--sub_worker_addresses` | string | "127.0.0.1:8006" | 子图地址列表（逗号分隔） |
| `--top_k`                | int32  | 100              | 返回前 K 个商品    |
| `--enable_timing_stats`  | bool   | true             | 是否启用详细时延统计   |

## 8. 端口分配

| 服务                | 端口   | 说明        |
| ----------------- | ---- | --------- |
| RankServiceMaster | 8005 | BRPC 服务端口 |
| RankServiceSub    | 8006 | 子图服务端口    |

## 9. 数据流

```
Client              RankMaster (:8005)           RankSub #0..N (:8006)
  │                        │                           │
  │ RankMasterRequest      │                           │
  │ (key, skus, payload)   │                           │
  │───────────────────────▶│                           │
  │                        │                           │
  │                        │  1. Parse SKUs            │
  │                        │     [100456,200789,...]    │
  │                        │                           │
  │                        │  2. Hash distribute       │
  │                        │     #0: [100456, ...]     │
  │                        │     #1: [200789, ...]     │
  │                        │     #N: [300123, ...]     │
  │                        │                           │
  │                        │  3. Parallel call ────────▶│
  │                        │     RankSubRequest         │
  │                        │     (key, skus_sub)        │
  │                        │                           │
  │                        │              ┌────────────│
  │                        │  4. Collect  │ Sub reads  │
  │                        │     scores   │ KVWorker   │
  │                        │◀─────────────│ & scores   │
  │                        │              └────────────│
  │                        │                           │
  │                        │  5. Top-K select          │
  │                        │     [200789,100456,...]    │
  │                        │                           │
  │ RankMasterResponse     │                           │
  │ (candidates: [...])    │                           │
  │◀───────────────────────│                           │
```

## 10. 错误处理与边界情况

| 场景                    | 行为                           |
| --------------------- | ---------------------------- |
| **空 user\_feat\_key** | 记录 ERROR，返回空响应               |
| **空 skus**            | 记录 ERROR，返回空响应               |
| **SKU 解析失败**          | 跳过无效 token，记录 WARNING        |
| **子图调用失败**            | 跳过该子图结果，记录 ERROR，其他子图正常归并    |
| **所有子图失败**            | 返回空 candidates 列表            |
| **部分子图慢**             | 延迟取决于最慢的子图（5s 超时兜底）          |
| **子图数量 > 地址数量**       | 循环使用地址（RoundRobin）           |
| **Channel 初始化失败**     | 保留空 Channel，后续调用会失败          |
| **动态扩缩容**             | 需重启 RankMaster 以更新 Channel 池 |

## 11. 演进规划

| 版本       | 特性                                          |
| -------- | ------------------------------------------- |
| **V1.0** | 核心功能：哈希分片 + 并行调用 + Top-K 归并                 |
| **V1.1** | 动态发现：集成 Discovery Service，自动感知 RankSub 实例变化 |
| **V1.2** | 超时控制：每个子图独立超时，部分超时不阻塞整体                     |
| **V1.3** | 重试机制：子图调用失败自动重试到其他实例                        |
| **V2.0** | 自适应分片：根据子图负载动态调整分片策略                        |

