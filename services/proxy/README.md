# Proxy 服务（网关）

## 概述

Proxy 是 LingQuickRec 系统的**网关入口**，对外暴露 HTTP 接口，对内持有 FeatureService、RecallService、PrecalcService、RankServiceMaster 四个下游服务的连接。它负责接收外部推荐请求，按业务编排顺序依次调用下游服务，最终返回排序后的候选结果。

## 业务调用流程

```
外部 HTTP 请求
      │
      ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Stage 1: 获取特征（同步）                                           │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  POST → FeatureService:8003 → GetUserFeatures                │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
      │
      ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Stage 2: 召回 & 预计算（并行）                                      │
│  ┌────────────────────────────────┐  ┌────────────────────────────┐ │
│  │  POST → RecallService:8001     │  │  POST → PrecalcService:8004│ │
│  │  → Recall(sku_ids)            │  │  → Precalculate(key)      │ │
│  └────────────────────────────────┘  └────────────────────────────┘ │
│           │                                   │                     │
│           ▼                                   ▼                     │
│   返回 candidates[]                     返回 user_feat_key          │
└─────────────────────────────────────────────────────────────────────┘
      │
      ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Stage 3: 精排（同步）                                               │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  POST → RankServiceMaster:8005 → Rank                        │   │
│  │  入参：user_feat_key + sku_ids(candidates)                    │   │
│  │  返回：排序后的 candidates[]                                  │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
      │
      ▼
   返回 HTTP Response
```

### 阶段时序说明

| 阶段 | 调用方式 | 依赖服务 | 说明 |
|------|---------|---------|------|
| Stage 1: 特征获取 | **同步阻塞** | FeatureService | 必须拿到用户特征后才能进行后续操作 |
| Stage 2a: 召回 | **异步并行** | RecallService | 与 Stage 2b 同时发起，互不依赖 |
| Stage 2b: 预计算 | **异步并行** | PrecalcService | 与 Stage 2a 同时发起，互不依赖 |
| Stage 3: 精排 | **同步阻塞** | RankServiceMaster | 必须等 Stage 2a/2b 都完成后才能执行 |

## 服务依赖

| 下游服务 | Proto Service | 默认地址 | 默认超时 |
|---------|--------------|---------|---------|
| FeatureService | `FeatureService` | `127.0.0.1:8003` | 3000ms |
| RecallService | `RecallService` | `127.0.0.1:8001` | 5000ms |
| PrecalcService | `PrecalcService` | `127.0.0.1:8004` | 5000ms |
| RankServiceMaster | `RankMasterService` | `127.0.0.1:8005` | 5000ms |

## 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8080 | Proxy HTTP 服务监听端口 |
| `--feature_service_addr` | "127.0.0.1:8003" | FeatureService 地址 |
| `--recall_service_addr` | "127.0.0.1:8001" | RecallService 地址 |
| `--precalc_service_addr` | "127.0.0.1:8004" | PrecalcService 地址 |
| `--rank_service_addr` | "127.0.0.1:8005" | RankServiceMaster 地址 |
| `--feature_timeout_ms` | 3000 | Feature 调用超时 (ms) |
| `--recall_timeout_ms` | 5000 | Recall 调用超时 (ms) |
| `--precalc_timeout_ms` | 5000 | Precalc 调用超时 (ms) |
| `--rank_timeout_ms` | 10000 | Rank 调用超时 (ms) |
| `--enable_timing_stats` | true | 是否打印阶段时延统计 |
| `--logtostderr` | false | 日志输出到 stderr |

## API 接口

### Recommend

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

**响应体：**

```json
{
  "candidates": [100001, 100002, 100003, ...]
}
```

## 启动方式

```bash
# 直接启动
./build/proxy_server \
    --server_port=8080 \
    --feature_service_addr="feature:8003" \
    --recall_service_addr="recall:8001" \
    --precalc_service_addr="precalc:8004" \
    --rank_service_addr="rank:8005" \
    --enable_timing_stats=true \
    --logtostderr

# Docker
docker run -p 8080:8080 lingquickrec/proxy:latest
```

## 可观测性

Proxy 在每次请求处理中记录以下时延指标（`--enable_timing_stats=true` 时打印）：

```
[Proxy] Timing breakdown:
  feature_cost=12.34 ms
  recall_cost=45.67 ms
  precalc_cost=89.01 ms
  rank_cost=234.56 ms
  total_cost=381.58 ms
```

同时所有下游 RPC 调用携带 `trace_id`，支持全链路追踪。

## 错误处理

| 场景 | 行为 |
|------|------|
| Feature 调用失败 | 直接返回 502，不继续后续流程 |
| Recall 或 Precalc 调用失败 | 记录错误日志，仍尝试执行另一条路径 |
| Rank 调用失败 | 返回 502，无候选结果 |
| 下游超时 | 返回 504 Gateway Timeout |
