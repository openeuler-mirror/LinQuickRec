# Docker 构建指南

## 目录结构

```
deploy/docker/
├── docker-compose.yml          # 构建编排文件
├── .env                        # 构建参数
├── push-to-registry.sh         # 推送镜像到私有 registry
├── README.md                   # 本文档
├── discovery/
│   ├── Dockerfile              # Discovery 服务镜像
│   └── entrypoint.sh           # 启动 Discovery
├── etcd/
│   ├── Dockerfile              # etcd 镜像
│   └── entrypoint.sh           # 单节点 etcd 启动 + discovery_client
├── feature/
│   ├── Dockerfile              # Feature 服务镜像（mock）
│   └── entrypoint.sh           # 启动 Feature + discovery_client
├── kv_worker/
│   ├── Dockerfile              # 元戎 Datasystem Worker 镜像
│   ├── entrypoint.sh           # 启动 datasystem + discovery_client
│   └── start_datasystem.sh     # datasystem 启动脚本
├── precalc/
│   ├── Dockerfile              # Precalc 服务镜像
│   └── entrypoint.sh           # 启动 Precalc + discovery_client
├── proxy/
│   ├── Dockerfile              # Proxy 网关镜像
│   └── entrypoint.sh           # 启动 Proxy + discovery_client
├── rank-master/
│   ├── Dockerfile              # RankMaster 服务镜像
│   └── entrypoint.sh           # 启动 RankMaster + discovery_client
├── rank-sub/
│   ├── Dockerfile              # RankSub 服务镜像
│   └── entrypoint.sh           # 启动 RankSub + discovery_client
└── recall/
    ├── Dockerfile              # Recall 服务镜像（vLLM / novllm 双模式）
    ├── entrypoint.sh           # 启动 vLLM → 等待 → 启动 Recall
    ├── start_vllm.sh           # vLLM 前台启动
    └── start_vllm_back.sh      # vLLM 后台启动
```

## 构建镜像

### 1. 配置 `.env`

编辑 `deploy/docker/.env` 设置构建参数：

```bash
# 若使用vLLM，该参数需要设置为false
ENABLE_VLLM=false

# 若使用vLLM，下列两个参数需要填写，用于pip install下载必要的包
# 若不使用vLLM，则无需填写
PROXY_IP=
PROXY_PORT=3128
```

### 2. 执行构建

```bash
cd deploy/docker

# novllm 模式（默认）
docker compose build

# vLLM 模式（在 .env 中设置 ENABLE_VLLM=true 后）
docker compose build

# 附带 discovery-server
docker compose --profile discovery-server build

# 构建指定服务
docker compose build recall-service
```

产物镜像 `<name>:latest` 存储在本地 Docker 缓存中：

```bash
docker images | grep linquickrec
```

## 推送镜像

构建完成后，使用 `push-to-registry.sh` 推送到私有镜像仓库：

```bash
# 必须通过-r/--registry参数指定私有仓库的<ip:port>

# dry-run 用来查看将要运行的命令和处理的镜像，不真正运行
# 建议先尝试一下--dry-run来看看结果是否符合预期
bash ./push-to-registry.sh -r 192.168.0.1:5000 --dry-run all

# 推送全部
bash ./push-to-registry.sh -r 192.168.0.1:5000
bash ./push-to-registry.sh -r 192.168.0.1:5000 all

# 推送单个
bash ./push-to-registry.sh -r 192.168.0.1:5000 recall
```

**清理镜像**：

```bash
# 删除构建产物（镜像）
docker compose down --rmi all

# 删除悬空镜像
docker image prune -f
```

## 验证镜像

### 集成测试（内置测试二进制）

以下服务在构建时包含单进程集成测试，无需外部依赖即可运行：

| 服务 | 验证命令 |
|------|---------|
| proxy | `docker run --rm linquickrec/proxy:latest test` |
| recall | `docker run --rm linquickrec/recall:latest ./build/bin/recall_integration_test` |
| precalc | `docker run --rm linquickrec/precalc:latest ./build/bin/precalc_test` |

测试通过时输出 `=== All ... Tests Passed ===`。

### 手动验证

以下服务无内置测试，可通过启动容器并探测端口来验证：

```bash
# Feature 服务
docker run --rm -p 8001:8001 linquickrec/feature:latest &
sleep 2 && nc -z localhost 8001 && echo "Feature OK" && kill %1

# RankMaster 服务
docker run --rm -p 8004:8004 linquickrec/rank-master:latest &
sleep 2 && nc -z localhost 8004 && echo "RankMaster OK" && kill %1

# RankSub 服务
docker run --rm -p 8005:8005 linquickrec/rank-sub:latest &
sleep 2 && nc -z localhost 8005 && echo "RankSub OK" && kill %1

# etcd
docker run --rm -p 2379:2379 linquickrec/etcd:latest &
sleep 3 && etcdctl endpoint health && kill %1

# Discovery 服务
docker run --rm -p 8100:8100 linquickrec/discovery:latest &
sleep 2 && nc -z localhost 8100 && echo "Discovery OK" && kill %1

# KV Worker（需要 etcd）
docker run --rm --privileged --ipc=host -v /dev/shm:/dev/shm \
  -e worker_address="127.0.0.1:31501" \
  -e etcd_address="127.0.0.1:2379" \
  -e enable_urma=0 \
  linquickrec/kv-worker:latest &
sleep 3 && docker ps | grep kv-worker && echo "KV Worker OK" && kill %1
```

## 基础镜像

所有服务基于 `linquickrec/base:latest`，包含 brpc、protobuf、abseil-cpp、gflags、leveldb、rapidjson 等依赖。


