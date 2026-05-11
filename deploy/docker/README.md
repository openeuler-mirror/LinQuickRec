# Docker 部署指南

## 架构总览

```
                          ┌──────────────────────────────────────────────────┐
                          │                 lingquickrec-net                 │
                          │              (Docker bridge 网络)                │
                          │                                                  │
 Client ──── :8080 ──► ┌──────────┐                                         │
                        │  Proxy   │───► FeatureService  :8003              │
                        │  :8080   │───► RecallService   :8001 (+vLLM :8000)│
                        │          │───► PrecalcService  :8004              │
                        │          │───► RankMaster      :8005              │
                        └──────────┘         │                              │
                                             │ fan-out                      │
                                    ┌────────┼────────┐                     │
                                    ▼        ▼        ▼                     │
                               RankSub₁  RankSub₂  RankSub₃  :8006 × N     │
                          │                                              │
                          │  DiscoveryServer :8100  (服务注册/发现)         │
                          └──────────────────────────────────────────────────┘

外部依赖（容器外）：
  - KVWorker   141.61.84.245:31501/31502
  - ETCD       141.61.84.245:2379
```

## 目录结构

```
deploy/docker/
├── docker-compose.yml      # 主编排文件（7 个服务）
├── .env                    # 环境变量配置（对应 K8s ConfigMap）
├── recall/
│   ├── Dockerfile          # Recall 服务镜像（含 vLLM + Qwen3-0.6B）
│   └── entrypoint.sh       # 启动 vLLM → 等待就绪 → 启动 Recall 服务
├── precalc/
│   ├── Dockerfile          # Precalc 服务镜像
│   └── entrypoint.sh       # 启动 Precalc 服务
├── rank-master/
│   ├── Dockerfile          # RankMaster 服务镜像
│   └── entrypoint.sh       # 等待 RankSub 就绪 → 启动 RankMaster 服务
├── rank-sub/
│   ├── Dockerfile          # RankSub 服务镜像
│   └── entrypoint.sh       # 启动 RankSub 服务
├── feature/
│   ├── Dockerfile          # Feature 服务镜像（mock）
│   └── entrypoint.sh       # 启动 Feature 服务
└── proxy/
    ├── Dockerfile          # Proxy 网关镜像
    └── entrypoint.sh       # 启动 Gateway 代理服务
```

## 前置条件

| 依赖 | 说明 |
|------|------|
| Docker + Compose v2 | `docker compose` 命令可用 |
| `brpc_base:latest` | 基础镜像，需提前构建或导入（包含 brpc、protobuf、gRPC、abseil 等依赖） |
| NVIDIA GPU + nvidia-container-toolkit | Recall 服务运行 vLLM 需要 GPU |
| 宿主机 CMake 工具链 | 预编译 discovery_server / discovery_client 二进制 |

## 快速开始

### 1. 预编译 Discovery 二进制

discovery 的 Dockerfile 依赖宿主机预编译产物，需先构建：

```bash
# 在项目根目录下
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make discovery_server discovery_client -j$(nproc)
# 产物位于 build/bin/discovery_server 和 build/bin/discovery_client
```

### 2. 一键启动

```bash
cd deploy/docker
docker compose up --build -d
```

### 3. 验证

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
| discovery-server | discovery-server | 8100 | lingquickrec-discovery | 服务注册与发现中心 |
| feature-service | feature-service | 8003 | lingquickrec-feature | 特征服务（mock） |
| recall-service | recall-service | 8001 | lingquickrec-recall | 召回服务（含 vLLM，需 GPU） |
| precalc-service | precalc-service | 8004 | lingquickrec-precalc | 预计算服务 |
| rank-sub-service | — (动态) | 8006 | lingquickrec-rank-sub | 排序子服务，默认 3 副本 |
| rank-master-service | rank-master-service | 8005 | lingquickrec-rank-master | 排序主服务 |
| proxy-service | proxy-service | 8080 | lingquickrec-proxy | 系统入口网关 |

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
SUB_WORKER_ADDRESSES=rank-sub-service:8006  # Docker 内部 DNS 自动解析所有副本

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

## 基础镜像

所有服务基于 `brpc_base:latest`，包含 brpc、protobuf、gRPC、abseil-cpp、gflags、leveldb、rapidjson、CURL 等依赖。

构建基础镜像需要在宿主机上提前准备好，不在本项目范围内。

## Dockerfile 说明

### Recall（recall/Dockerfile）

Recall 服务额外包含 vLLM 和模型文件：

1. 安装 vLLM wheel 包（`vllm-0.11-0rc6+cu129-cp311-cp311-linux_aarch64.whl`）
2. 复制 vLLM 启动脚本（`start_vllm_back.sh`、`start_vllm.sh`）
3. 复制模型文件（`Qwen3-0.6B/`、`Qwen3-8B/`）
4. 编译 Recall 服务
5. 暴露端口 8001（brpc）和 8000（vLLM）

注意：Dockerfile 中的 COPY 路径（如 `/home/w00921547/share/`）是构建机器上的绝对路径，需要在构建机器上执行。

### Precalc（precalc/Dockerfile）

1. 复制 proto、common、PrecalcService 源码
2. CMake 编译
3. 暴露端口 8004

### RankMaster（rank-master/Dockerfile）

1. 复制 proto、common、RankServiceMaster 源码
2. CMake 编译
3. 暴露端口 8005

### RankSub（rank-sub/Dockerfile）

1. 复制 proto、common、RankServiceSub 源码
2. CMake 编译
3. 暴露端口 8006

### Feature（feature/Dockerfile）

1. 复制 proto、common、FeatureService 源码
2. CMake 编译
3. 暴露端口 8003

### Proxy（proxy/Dockerfile）

1. 复制 proto、common、Proxy 源码
2. CMake 编译
3. 暴露端口 8080

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
| `SERVER_PORT` | 8001 | brpc 监听端口 |
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
| `SERVER_PORT` | 8004 | brpc 监听端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 地址 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 地址 |
| `TTL_SECONDS` | 5 | KV 缓存 TTL |
| `PRECALC_RESULT_SIZE_MB` | 8.5 | 预计算结果大小 |
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
| `SERVER_PORT` | 8005 | brpc 监听端口 |
| `SUB_WORKER_COUNT` | 3 | RankSub 工作线程数 |
| `SUB_WORKER_ADDRESSES` | rank-sub-service:8006 | RankSub 服务地址 |
| `TOP_K` | 100 | 返回 Top-K 结果 |
| `RANK_SUB_HOST` | rank-sub-service | RankSub 主机名（用于健康检查） |
| `RANK_SUB_PORT` | 8006 | RankSub 端口（用于健康检查） |
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
| `SERVER_PORT` | 8006 | brpc 监听端口 |
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
| `SERVER_PORT` | 8003 | brpc 监听端口 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

### proxy/entrypoint.sh

```bash
# 启动流程：
1. 启动 gateway_server，配置所有下游服务地址
2. 启动 discovery_client 注册服务
```

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8080 | brpc 监听端口 |
| `FEATURE_SERVICE_ADDR` | feature-service:8003 | Feature 服务地址 |
| `RECALL_SERVICE_ADDR` | recall-service:8001 | Recall 服务地址 |
| `PRECALC_SERVICE_ADDR` | precalc-service:8004 | Precalc 服务地址 |
| `RANK_SERVICE_ADDR` | rank-master-service:8005 | RankMaster 服务地址 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 服务发现地址 |

## 手动构建单个镜像

如果不使用 docker-compose，也可以单独构建和运行：

```bash
# 在项目根目录下执行（因为需要 COPY proto/ 和 common/）

# 构建 Recall（需要 GPU 构建机器，且文件路径需匹配）
docker build -f deploy/docker/recall/Dockerfile -t lingquickrec/recall:latest .

# 构建 Precalc
docker build -f deploy/docker/precalc/Dockerfile -t lingquickrec/precalc:latest .

# 构建 RankMaster
docker build -f deploy/docker/rank-master/Dockerfile -t lingquickrec/rank-master:latest .

# 构建 RankSub
docker build -f deploy/docker/rank-sub/Dockerfile -t lingquickrec/rank-sub:latest .

# 构建 Feature
docker build -f deploy/docker/feature/Dockerfile -t lingquickrec/feature:latest .

# 构建 Proxy
docker build -f deploy/docker/proxy/Dockerfile -t lingquickrec/proxy:latest .
```

### 推送到镜像仓库

```bash
docker tag lingquickrec/recall:latest <registry>/lingquickrec/recall:latest
docker push <registry>/lingquickrec/recall:latest
```
