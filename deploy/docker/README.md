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
| recall-service | recall-service | 8002 | linquickrec-recall | 召回服务（含 vLLM，需 GPU） |
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

## Dockerfile 说明

### Discovery（discovery/Dockerfile）

编译 `discovery_server`，监听 8100 端口，提供服务注册/发现/心跳 RPC。

### Proxy（proxy/Dockerfile）

编译 `proxy_server` 和 `discovery_client`。通过 sidecar 模式向 discovery-server 注册自身，并通过 discovery 动态发现下游实例。

### Recall（recall/Dockerfile）

Recall 服务与 vLLM 同容器部署，额外安装 vLLM wheel 包和模型文件（Qwen3-0.6B）：

1. 安装 PyTorch + vLLM
2. 复制 vLLM 启动脚本（`start_vllm_back.sh`、`start_vllm.sh`）
3. 复制模型文件（`Qwen3-0.6B/`、`Qwen3-8B/`）
4. 编译 recall_server
5. 暴露端口 8002（brpc）和 8000（vLLM）

### Precalc（precalc/Dockerfile）

编译 `precalc_server`，监听 8003 端口，将用户特征预计算结果写入 KVWorker。

### RankMaster（rank-master/Dockerfile）

编译 `rank_master_server`，监听 8004 端口，将候选商品分发给多个 RankSub 并行打分后归并结果。

### RankSub（rank-sub/Dockerfile）

编译 `rank_sub_server`，监听 8005 端口，从 KVWorker 读取特征 tensor 并对分配到的 SKU 打分。

### Feature（feature/Dockerfile）

1. 复制 proto、common、FeatureService 源码
2. CMake 编译
3. 暴露端口 8001

## EntryPoint 说明

每个 entrypoint.sh 通过环境变量配置服务参数，环境变量有默认值，也可通过 `.env` 或 `environment` 注入覆盖。

### recall/entrypoint.sh

```bash
# 启动流程：
1. 后台启动 vLLM（start_vllm_back.sh）
2. 轮询 http://127.0.0.1:8000/health 等待 vLLM 就绪（最长 120 秒）
3. 启动 recall_server
4. 启动 discovery_client 注册服务
```

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

### precalc/entrypoint.sh

```bash
# 启动流程：
1. 启动 precalc_server
2. 启动 discovery_client 注册服务
```

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

### rank-master/entrypoint.sh

```bash
# 启动流程：
1. 等待 RankSub 服务 TCP 端口就绪（最长 120 秒，超时也会继续启动）
2. 启动 rank_master_server
3. 启动 discovery_client 注册服务
```

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8004 | brpc 监听端口 |
| `SUB_WORKER_COUNT` | 3 | RankSub 工作线程数 |
| `SUB_WORKER_ADDRESSES` | rank-sub-service:8005 | RankSub 服务地址 |
| `TOP_K` | 100 | 返回 Top-K 结果 |
| `RANK_SUB_HOST` | rank-sub-service | RankSub 主机名 |
| `RANK_SUB_PORT` | 8005 | RankSub 端口 |
| `RANK_SUB_STARTUP_TIMEOUT` | 120 | 等待 RankSub 就绪秒数 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### rank-sub/entrypoint.sh

```bash
# 启动流程：
1. 启动 rank_sub_server
2. 启动 discovery_client 注册服务
```

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8005 | brpc 监听端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 地址 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 地址 |
| `SCORING_DELAY_MS` | 100 | 打分延迟 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### feature/entrypoint.sh

```bash
# 启动流程：
1. 启动 feature_server（mock）
2. 启动 discovery_client 注册服务
```

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8001 | brpc 监听端口 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### proxy/entrypoint.sh

```bash
# 启动流程：
1. 启动 proxy_server，配置所有下游服务地址
2. 启动 discovery_client 注册服务
```

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8080 | brpc 监听端口 |
| `FEATURE_SERVICE_ADDR` | feature-service:8001 | Feature 服务地址 |
| `RECALL_SERVICE_ADDR` | recall-service:8002 | Recall 服务地址 |
| `PRECALC_SERVICE_ADDR` | precalc-service:8003 | Precalc 服务地址 |
| `RANK_SERVICE_ADDR` | rank-master-service:8004 | RankMaster 服务地址 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

## 手动构建单个镜像

如果不使用 docker-compose，也可以单独构建和运行：

在项目根目录下执行：

```bash
# 构建 Recall（需要 GPU 构建机器，文件路径需匹配）
docker build -f deploy/docker/recall/Dockerfile -t linquickrec/recall:latest .

# 构建 Precalc
docker build -f deploy/docker/precalc/Dockerfile -t linquickrec/precalc:latest .

# 构建 RankMaster
docker build -f deploy/docker/rank-master/Dockerfile -t linquickrec/rank-master:latest .

# 构建 RankSub
docker build -f deploy/docker/rank-sub/Dockerfile -t linquickrec/rank-sub:latest .

# 构建 Feature
docker build -f deploy/docker/feature/Dockerfile -t linquickrec/feature:latest .

# 构建 Proxy
docker build -f deploy/docker/proxy/Dockerfile -t linquickrec/proxy:latest .

# 构建 Discovery
docker build -f deploy/docker/discovery/Dockerfile -t linquickrec/discovery:latest .
```

## 本地运行

### 服务发现中心

```bash
docker run -d --name discovery \
    -p 8100:8100 \
    linquickrec/discovery:latest
```

### Proxy（依赖 discovery-server）

```bash
docker run -d --name proxy \
    -p 8080:8080 \
    -e DISCOVERY_ADDR=host.docker.internal:8100 \
    linquickrec/proxy:latest
```

### Precalc

```bash
docker run -d --name precalc \
    -p 8003:8003 \
    linquickrec/precalc:latest
```

### RankSub

```bash
docker run -d --name rank-sub \
    -p 8005:8005 \
    linquickrec/rank-sub:latest
```

### RankMaster（需 RankSub 已运行）

```bash
docker run -d --name rank-master \
    -p 8004:8004 \
    -e RANK_SUB_HOST=host.docker.internal \
    -e SUB_WORKER_ADDRESSES=host.docker.internal:8005 \
    linquickrec/rank-master:latest
```

### Recall（需 GPU）

```bash
docker run -d --gpus all --name recall \
    -p 8002:8002 -p 8000:8000 \
    linquickrec/recall:latest
```

## 端到端演示

`examples/` 目录提供完整的 docker-compose 编排，参见 [services/discovery/examples/README.md](../../services/discovery/examples/README.md)。
