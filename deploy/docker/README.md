# Docker 部署指南

## 架构总览

```
                          ┌────────────────────────────────────────────────────┐
                          │                 linquickrec-net                   │
                          │                 (Docker bridge)                    │
                          │                                                    │
 Client :8080 ──► ┌────────────┐                                               │
                  │   Proxy    │──── Discovery (8100)                          │
                  │  (gateway) │                                               │
                  └─────┬──────┘                                               │
                        │ discover downstream instances                        │
                        ▼                                                      │
         ┌──────────────┼──────────────┐                                       │
         ▼              ▼              ▼                                       │
  Feature (x1)    Recall (xN)    Precalc (xN)                                  │
  :8001             :8002           :8003                                      │
  [pending]         + vLLM                                                     │
                    :8000          KVWorker                                    │
         │              │            :31502                                    │
         │              ▼                                                      │
         │         KVWorker                                                    │
         │         :31501                                                      │
         └──────────────┬──────────────┘                                       │
                        ▼                                                      │
                 RankMaster (xN)                                               │
                 :8004                                                         │
                        │                                                      │
                        ▼                                                      │
                 RankSub (xN)  ◄──── KVWorker :31502                           │
                 :8005                                                         │
                          │                                                    │
                          │                                                    │
                  Discovery Server :8100                                       │
                          └────────────────────────────────────────────────────┘
```

## 目录结构

```
deploy/docker/
├── docker-compose.yml          # 主编排文件（8 个服务）
├── .env                        # 环境变量配置
├── README.md                   # 本文档
├── discovery/
│   ├── Dockerfile              # Discovery 服务镜像
│   ├── entrypoint.sh           # 启动 Discovery 服务
│   └── examples/
│       ├── Dockerfile          # 端到端演示镜像（pseudo_service + test tools）
│       └── entrypoint.sh       # 注册伪服务到 discovery server
├── feature/
│   ├── Dockerfile              # Feature 服务镜像（mock）
│   └── entrypoint.sh           # 启动 Feature 服务
├── kv_worker/
│   ├── Dockerfile              # 元戎数据系统 Worker 镜像
│   └── entrypoint.sh           # 启动 datasystem
├── precalc/
│   ├── Dockerfile              # Precalc 服务镜像
│   └── entrypoint.sh           # 启动 Precalc 服务
├── proxy/
│   ├── Dockerfile              # Proxy 网关镜像
│   └── entrypoint.sh           # 启动 Proxy + discovery_client sidecar
├── rank-master/
│   ├── Dockerfile              # RankMaster 服务镜像
│   └── entrypoint.sh           # 等待 RankSub 就绪 → 启动 RankMaster
├── rank-sub/
│   ├── Dockerfile              # RankSub 服务镜像
│   └── entrypoint.sh           # 启动 RankSub 服务
└── recall/
    ├── Dockerfile              # Recall 服务镜像（含 vLLM + Qwen3-0.6B）
    └── entrypoint.sh           # 启动 vLLM → 等待就绪 → 启动 Recall
```

## 前置条件

| 依赖 | 说明 |
|------|------|
| Docker + Compose v2 | `docker compose` 命令可用 |
| `linquickrec/base:latest` | 基础镜像，需提前构建或导入（包含 brpc、protobuf、gRPC、abseil 等依赖） |
| NVIDIA GPU + nvidia-container-toolkit | Recall 服务运行 vLLM 需要 GPU |

## 快速开始

### 1. 一键启动

所有服务均在容器内编译，无需宿主机安装 brpc 或预编译任何二进制：

```bash
cd deploy/docker
docker compose up --build -d
```

### 2. 验证

```bash
# 查看所有容器状态
docker compose ps

# 检查 proxy 网关是否就绪
curl http://localhost:8080  # 或根据实际接口测试

# 查看 discovery 注册的服务
docker compose logs discovery-server
```

## docker-compose 服务清单

| 服务 | 容器名 | 端口 | 镜像 | 说明 |
|------|--------|------|------|------|
| discovery-server | discovery-server | 8100 | linquickrec-discovery | 服务注册与发现中心 |
| feature-service | feature-service | 8001 | linquickrec-feature | 特征服务（mock） |
| recall-service | recall-service | 8002 | linquickrec-recall | 召回服务（需 GPU） |
| vLLM | recall-service | 8000 | — | 大模型推理服务，与 recall 同容器部署 |
| precalc-service | precalc-service | 8003 | linquickrec-precalc | 预计算服务 |
| rank-sub-service | — (动态) | 8005 | linquickrec-rank-sub | 排序子服务，默认 3 副本 |
| rank-master-service | rank-master-service | 8004 | linquickrec-rank-master | 排序主服务 |
| proxy-service | proxy-service | 8080 | linquickrec-proxy | 系统入口网关 |

### 启动顺序

```
discovery-server (healthcheck 通过)
        │
        ├── feature-service    ─┐
        ├── recall-service     ─┤ 并行启动
        ├── precalc-service    ─┤
        └── rank-sub-service   ─┘
                │
        rank-master-service (等待 rank-sub TCP 就绪)
                │
        proxy-service (依赖所有上游服务)
```

## 配置管理

所有配置项集中在 `.env` 文件中，修改后重新 `docker compose up -d` 即可生效。

### 关键配置项

```bash
# ---- 外部依赖 ----
KVWORKER_HOST=141.61.84.245     # KVWorker 地址
KVWORKER_PORT=31502             # KVWorker 端口
ETCD_ADDRESS=141.61.84.245:2379 # ETCD 地址

# ---- vLLM 模型 ----
MODEL_NAME=/app/models/Qwen3-0.6B/  # 容器内模型路径
VLLM_TIMEOUT_MS=100000              # vLLM 请求超时

# ---- Rank 扩展 ----
RANK_SUB_REPLICAS=3                  # rank-sub 副本数
SUB_WORKER_COUNT=3                   # rank-master 连接的 worker 数（需与副本数一致）
SUB_WORKER_ADDRESSES=rank-sub-service:8005  # Docker 内部 DNS 自动解析所有副本

# ---- 网关 ----
PROXY_PORT=8080                      # 对外暴露的代理端口
```

### 扩展 RankSub 副本

以扩展到 5 个为例，修改 `.env`：

```bash
RANK_SUB_REPLICAS=5
SUB_WORKER_COUNT=5
```

然后重新启动：

```bash
docker compose up -d
```

> `rank-sub-service` 的 Docker 内部 DNS 会自动解析到所有副本 IP，`SUB_WORKER_ADDRESSES` 无需改为逗号分隔列表。

## 常用操作

```bash
# ---- 启停 ----
docker compose up --build -d          # 构建并启动全部
docker compose up -d                  # 启动（不重新构建）
docker compose down                   # 停止并移除容器
docker compose restart proxy-service  # 重启单个服务

# ---- 单独构建/启动 ----
docker compose build recall-service   # 只构建 recall
docker compose up -d recall-service   # 只启动 recall

# ---- 查看日志 ----
docker compose logs -f                        # 全部日志（实时跟踪）
docker compose logs -f proxy-service          # 单个服务日志
docker compose logs --tail=50 recall-service  # 最近 50 行

# ---- 扩缩容 ----
docker compose up -d --scale rank-sub-service=5  # 临时指定副本数

# ---- 状态检查 ----
docker compose ps                     # 容器状态
docker compose top                    # 容器内进程
```

每个目录包含一个 `Dockerfile` 和一个 `entrypoint.sh`。

## 基础镜像

所有服务基于 `linquickrec/base:latest`，包含 brpc、protobuf、abseil-cpp、gflags、leveldb、rapidjson 等依赖。

## 组件说明

每个组件目录包含一个 `Dockerfile` 和一个 `entrypoint.sh`。entrypoint.sh 通过环境变量配置参数，变量均有默认值，可通过 `.env` 或 `environment` 注入覆盖。

### Discovery

编译 `discovery_server`，提供服务注册/发现/心跳 RPC。

启动流程：启动 discovery_server → 等待请求。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8100 | brpc 监听端口 |
| `HEARTBEAT_CHECK_INTERVAL_MS` | 1000 | 健康检查扫描间隔 (ms) |
| `HEARTBEAT_GRACE_FACTOR` | 2.0 | 心跳超时倍数 |
| `CLEANUP_FACTOR` | 5.0 | 清理倍数 |

### Proxy

编译 `proxy_server` 和 `discovery_client`。通过 sidecar 向 discovery 注册，通过 discovery 动态发现下游实例。支持 `test` 参数运行集成测试。

启动流程：启动 proxy_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8080 | HTTP 监听端口 |
| `FEATURE_SERVICE_ADDR` | feature-service:8001 | Feature 服务地址 |
| `RECALL_SERVICE_ADDR` | recall-service:8002 | Recall 服务地址 |
| `PRECALC_SERVICE_ADDR` | precalc-service:8003 | Precalc 服务地址 |
| `RANK_SERVICE_ADDR` | rank-master-service:8004 | RankMaster 服务地址 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### Recall

与 vLLM 同容器部署，安装 vLLM wheel 包和 Qwen3-0.6B 模型文件。暴露端口 8002（brpc）和 8000（vLLM）。

启动流程：后台启动 vLLM → 轮询 health 等待就绪（最长 120s）→ 启动 recall_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8002 | brpc 监听端口 |
| `VLLM_BASE_URL` | http://127.0.0.1:8000 | vLLM 地址 |
| `VLLM_ENDPOINT` | /v1/chat/completions | vLLM 推理接口路径 |
| `MODEL_NAME` | /app/models/Qwen3-0.6B/ | 模型路径 |
| `VLLM_TIMEOUT_MS` | 100000 | vLLM 请求超时 |
| `SKU_COUNT` | 100 | SKU 数量 |
| `VLLM_PORT` | 8000 | vLLM 健康检查端口 |
| `VLLM_STARTUP_TIMEOUT` | 120 | vLLM 启动等待秒数 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### Precalc

编译 `precalc_server`，将用户特征预计算结果写入 KVWorker。

启动流程：启动 precalc_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8003 | brpc 监听端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 地址 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 地址 |
| `TTL_SECONDS` | 5 | KV 缓存 TTL |
| `PRECALC_RESULT_SIZE_MB` | 8.5 | 预计算结果大小 (MB) |
| `PAYLOAD_SIZE_KB` | 100 | Payload 大小 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### RankMaster

编译 `rank_master_server`，将候选商品分发给多个 RankSub 并行打分后归并。

启动流程：等待 RankSub 就绪（最长 120s）→ 启动 rank_master_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8004 | brpc 监听端口 |
| `SUB_WORKER_COUNT` | 3 | RankSub 数量 |
| `SUB_WORKER_ADDRESSES` | rank-sub-service:8005 | RankSub 地址 |
| `TOP_K` | 100 | 返回 Top-K 结果 |
| `RANK_SUB_HOST` | rank-sub-service | RankSub 主机名 |
| `RANK_SUB_PORT` | 8005 | RankSub 端口 |
| `RANK_SUB_STARTUP_TIMEOUT` | 120 | 等待 RankSub 就绪秒数 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### RankSub

编译 `rank_sub_server`，从 KVWorker 读取特征 tensor 并对分配到的 SKU 打分。

启动流程：启动 rank_sub_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8005 | brpc 监听端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 地址 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 地址 |
| `SCORING_DELAY_MS` | 100 | 打分延迟 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### Feature

编译 `feature_server`（mock），当前为桩实现。

启动流程：启动 feature_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8001 | brpc 监听端口 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### KVWorker

安装 `openyuanrong_datasystem` 包，运行 `start_datasystem.sh`。

启动流程：执行 start_datasystem.sh。

无环境变量配置。
