# Docker 构建指南

## 目录结构

```
deploy/docker/
├── discovery/            # 服务发现中心
├── proxy/                # 网关服务
├── recall/               # 召回服务（含 vLLM + Qwen3-0.6B）
├── precalc/              # 前置计算服务
├── rank-master/          # 精排主图服务
├── rank-sub/             # 精排子图服务
├── feature_service/      # 特征服务（待合入）
├── kv_worker/            # 元戎数据系统 Worker
├── examples/             # 端到端演示（pseudo_service + test tools）
└── README.md
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
5. 暴露端口 8001（brpc）和 8000（vLLM）

### Precalc（precalc/Dockerfile）

编译 `precalc_server`，监听 8004 端口，将用户特征预计算结果写入 KVWorker。

### RankMaster（rank-master/Dockerfile）

编译 `rank_master_server`，监听 8005 端口，将候选商品分发给多个 RankSub 并行打分后归并结果。

### RankSub（rank-sub/Dockerfile）

编译 `rank_sub_server`，监听 8006 端口，从 KVWorker 读取特征 tensor 并对分配到的 SKU 打分。

### FeatureService（feature_service/Dockerfile）

编译 `feature_server`，监听 8003 端口。实现待其他开发者合入。

### KVWorker（kv_worker/Dockerfile）

基于 pip 安装 `openyuanrong_datasystem`，运行 `start_datasystem.sh`。

### Examples（examples/Dockerfile）

构建 `discovery_client`、`pseudo_service`、`test_discover`、`test_register`、`test_heartbeat_cycle`，用于端到端演示。

## EntryPoint 说明

每个 entrypoint.sh 通过环境变量配置服务参数，均有默认值，可通过 Docker/K8s 注入覆盖。

### discovery/entrypoint.sh

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8100 | brpc 监听端口 |
| `HEARTBEAT_CHECK_INTERVAL_MS` | 1000 | 健康检查扫描间隔 (ms) |
| `HEARTBEAT_GRACE_FACTOR` | 2.0 | 心跳超时倍数 |
| `CLEANUP_FACTOR` | 5.0 | 清理倍数 |

### proxy/entrypoint.sh

支持 `test` 参数运行集成测试：

```bash
docker run linquickrec/proxy:latest test
```

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVICE_TYPE` | proxy | 服务发现注册名 |
| `SERVICE_PORT` | 8080 | HTTP 监听端口 |
| `DISCOVERY_ADDR` | discovery-server:8100 | Discovery Server 地址 |

### recall/entrypoint.sh

启动流程：后台启动 vLLM → 轮询 health 等待就绪 → 启动 recall_server。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8001 | brpc 监听端口 |
| `VLLM_BASE_URL` | http://127.0.0.1:8000 | vLLM 地址 |
| `VLLM_ENDPOINT` | /v1/chat/completions | vLLM 推理接口路径 |
| `MODEL_NAME` | /app/models/Qwen3-0.6B/ | 模型路径 |
| `VLLM_TIMEOUT_MS` | 5000 | vLLM 请求超时 |
| `SKU_COUNT` | 1000 | SKU 数量 |
| `VLLM_PORT` | 8000 | vLLM 健康检查端口 |
| `VLLM_STARTUP_TIMEOUT` | 120 | vLLM 启动等待秒数 |

### precalc/entrypoint.sh

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8004 | brpc 监听端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 地址 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 地址 |
| `TTL_SECONDS` | 5 | KV 缓存 TTL |
| `PRECALC_RESULT_SIZE_MB` | 8.5 | 预计算结果大小 (MB) |
| `PAYLOAD_SIZE_KB` | 100 | Payload 大小 |

### rank-master/entrypoint.sh

启动流程：等待 RankSub 就绪（最长 120s，超时仍继续启动）→ 启动 rank_master_server。

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8005 | brpc 监听端口 |
| `SUB_WORKER_COUNT` | 10 | RankSub 数量 |
| `SUB_WORKER_ADDRESSES` | rank-sub-service:8006 | RankSub 地址 |
| `TOP_K` | 100 | 返回 Top-K 结果 |
| `RANK_SUB_HOST` | rank-sub-service | RankSub 主机名 |
| `RANK_SUB_PORT` | 8006 | RankSub 端口 |
| `RANK_SUB_STARTUP_TIMEOUT` | 120 | 等待 RankSub 就绪秒数 |

### rank-sub/entrypoint.sh

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8006 | brpc 监听端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 地址 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 地址 |
| `SCORING_DELAY_MS` | 100 | 打分延迟 (ms) |

## 构建镜像

### 前提条件

- Docker 已安装
- 基础镜像 `linquickrec/base:latest` 已构建或导入
- Recall 服务额外需要：vLLM wheel 包、模型文件、vLLM 启动脚本

### 构建命令

在项目根目录下执行：

```bash
# 构建单个镜像
docker build -t linquickrec/discovery:latest \
    -f deploy/docker/discovery/Dockerfile .
docker build -t linquickrec/proxy:latest \
    -f deploy/docker/proxy/Dockerfile .
docker build -t linquickrec/precalc:latest \
    -f deploy/docker/precalc/Dockerfile .
docker build -t linquickrec/rank-master:latest \
    -f deploy/docker/rank-master/Dockerfile .
docker build -t linquickrec/rank-sub:latest \
    -f deploy/docker/rank-sub/Dockerfile .

# Recall（需 GPU 构建机器，文件路径需匹配）
docker build -t linquickrec/recall:latest \
    -f deploy/docker/recall/Dockerfile .

# 批量构建
for svc in discovery proxy precalc rank-master rank-sub; do
  docker build -t linquickrec/${svc}:latest \
    -f deploy/docker/${svc}/Dockerfile .
done
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
    -p 8004:8004 \
    linquickrec/precalc:latest
```

### RankSub

```bash
docker run -d --name rank-sub \
    -p 8006:8006 \
    linquickrec/rank-sub:latest
```

### RankMaster（需 RankSub 已运行）

```bash
docker run -d --name rank-master \
    -p 8005:8005 \
    -e RANK_SUB_HOST=host.docker.internal \
    -e SUB_WORKER_ADDRESSES=host.docker.internal:8006 \
    linquickrec/rank-master:latest
```

### Recall（需 GPU）

```bash
docker run -d --gpus all --name recall \
    -p 8001:8001 -p 8000:8000 \
    linquickrec/recall:latest
```

## 端到端演示

`examples/` 目录提供完整的 docker-compose 编排，参见 [services/discovery/examples/README.md](../../services/discovery/examples/README.md)。
