# Docker 构建指南

## 目录结构

```
deploy/docker/
├── recall/
│   ├── Dockerfile        # Recall 服务镜像（含 vLLM + Qwen3-0.6B）
│   └── entrypoint.sh     # 启动 vLLM → 等待就绪 → 启动 Recall 服务
├── precalc/
│   ├── Dockerfile        # Precalc 服务镜像
│   └── entrypoint.sh     # 启动 Precalc 服务
├── rank-master/
│   ├── Dockerfile        # RankMaster 服务镜像
│   └── entrypoint.sh     # 等待 RankSub 就绪 → 启动 RankMaster 服务
└── rank-sub/
    ├── Dockerfile        # RankSub 服务镜像
    └── entrypoint.sh     # 启动 RankSub 服务
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

## EntryPoint 说明

每个 entrypoint.sh 通过环境变量配置服务参数，环境变量有默认值，也可通过 Docker/K8s 注入覆盖。

### recall/entrypoint.sh

```bash
# 启动流程：
1. 后台启动 vLLM（start_vllm_back.sh）
2. 轮询 http://127.0.0.1:8000/health 等待 vLLM 就绪（最长 120 秒）
3. 启动 recall_server
```

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

```bash
# 启动流程：
1. 直接启动 precalc_server
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

### rank-master/entrypoint.sh

```bash
# 启动流程：
1. 等待 RankSub 服务 TCP 端口就绪（最长 120 秒，超时也会继续启动）
2. 启动 rank_master_server
```

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8005 | brpc 监听端口 |
| `SUB_WORKER_COUNT` | 10 | RankSub 工作线程数 |
| `SUB_WORKER_ADDRESSES` | rank-sub-service:8006 | RankSub 服务地址 |
| `TOP_K` | 100 | 返回 Top-K 结果 |
| `RANK_SUB_HOST` | rank-sub-service | RankSub 主机名（用于健康检查） |
| `RANK_SUB_PORT` | 8006 | RankSub 端口（用于健康检查） |
| `RANK_SUB_STARTUP_TIMEOUT` | 120 | 等待 RankSub 就绪秒数 |

### rank-sub/entrypoint.sh

```bash
# 启动流程：
1. 直接启动 rank_sub_server
```

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SERVER_PORT` | 8006 | brpc 监听端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 地址 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 地址 |
| `SCORING_DELAY_MS` | 100 | 打分延迟 |

## 构建镜像

### 前提条件

- Docker 已安装
- 基础镜像 `brpc_base:latest` 已构建或导入
- Recall 服务额外需要：vLLM wheel 包、模型文件、vLLM 启动脚本（路径见 Dockerfile 中的 COPY）

### 构建命令

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
```

### 构建所有镜像

```bash
for svc in recall precalc rank-master rank-sub; do
  docker build -f deploy/docker/${svc}/Dockerfile -t lingquickrec/${svc}:latest .
done
```

### 推送到镜像仓库

```bash
# 如果使用私有仓库，先打 tag 再推送
docker tag lingquickrec/recall:latest <registry>/lingquickrec/recall:latest
docker push <registry>/lingquickrec/recall:latest

# 对其他服务同理
```

## 本地运行（Docker 直接运行）

```bash
# Recall（需要 GPU）
docker run -d --gpus all --runtime=nvidia \
  --name recall \
  -p 8001:8001 -p 8000:8000 \
  lingquickrec/recall:latest

# Precalc
docker run -d --name precalc \
  -p 8004:8004 \
  lingquickrec/precalc:latest

# RankSub
docker run -d --name rank-sub \
  -p 8006:8006 \
  lingquickrec/rank-sub:latest

# RankMaster（需要 RankSub 已运行）
docker run -d --name rank-master \
  -p 8005:8005 \
  -e RANK_SUB_HOST=host.docker.internal \
  -e SUB_WORKER_ADDRESSES=host.docker.internal:8006 \
  lingquickrec/rank-master:latest
```

本地运行时 RankMaster 需要通过 `host.docker.internal` 或宿主机 IP 访问 RankSub。K8s 部署则无需此配置，通过 K8s Service 自动发现。
