# Docker 部署指南

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

## 快速开始

### 容器构建

所有服务均在容器内编译，无需宿主机安装 brpc 或预编译任何二进制：

前置依赖：
+ 基础镜像已完成编译
+ 在LinQuickRec的目录下的share目录，并将必要的两个whl文件+Qwen3-0.6B的模型文件放入share目录中

```bash
cd deploy/docker

# 容器构建命令
docker compose build

# 查看容器
docker images | grep linquickrec
```

**构建过程到此结束！！！**
**构建过程到此结束！！！**
**构建过程到此结束！！！**

若您不希望使用docker-compose拉起服务容器，而计划使用k8s，那么此时请参阅 [容器化部署（k8s）文档](../k8s/README.md)。

---

### 容器化部署（docker-compose）

本章节提供在单节点上部署一套LinQuickRec系统进行功能验证。

**启动容器：**

```bash
# 容器构建并启动
docker compose up --build -d

# 若容器已经构建完成，可直接启动
docker compose up -d
```

**验证容器启动成功：**

```bash
# 查看所有容器状态
docker compose ps

# 检查 proxy 网关是否就绪
curl http://localhost:8080  # 或根据实际接口测试

# 查看 discovery 注册的服务
docker compose logs discovery-server
```

**构建请求进行简单测试：**

LinQuickRec 根目录下的 `scripts/send_proxy_request.sh` 提供一个简单的请求，您可以查看该脚本，更改参数，并运行脚本来模拟单个请求进行测试。

```bash
# 从LinQuickRec项目根目录
cd scripts/
bash send_proxy_request.sh
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

所有配置项集中在 `.env` 文件中，修改后执行 `docker compose up -d` 即可生效。完整参数说明见 [CONFIG.md](../../CONFIG.md)。

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

每个组件目录包含一个 `Dockerfile` 和一个 `entrypoint.sh`。entrypoint.sh 通过环境变量配置参数，变量均有默认值，可通过 `.env` 或 `environment` 注入覆盖。完整参数列表及生效方式见 [CONFIG.md](../../CONFIG.md)。

### Discovery

编译 `discovery_server`，提供服务注册/发现/心跳 RPC。

启动流程：启动 discovery_server → 等待请求。

| 环境变量 | 默认值 | 配置级别 | 说明 |
|---------|--------|---------|------|
| `SERVER_PORT` | 8100 | 允许调整 | 监听端口 |
| `HEARTBEAT_CHECK_INTERVAL_MS` | 1000 | 不建议修改 | 健康检查扫描间隔 (ms) |
| `HEARTBEAT_GRACE_FACTOR` | 2.0 | 不建议修改 | 心跳超时倍数 |
| `CLEANUP_FACTOR` | 5.0 | 不建议修改 | 清理倍数 |

### Proxy

编译 `proxy_server` 和 `discovery_client`。通过 sidecar 向 discovery 注册，通过 discovery 动态发现下游实例。支持 `test` 参数运行集成测试。

启动流程：启动 proxy_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 配置级别 | 说明 |
|---------|--------|---------|------|
| `SERVER_PORT` | 8080 | 允许调整 | HTTP 监听端口 |
| `DISCOVERY_ADDR` | — | 必须指定 | 服务发现地址 |

> 下游地址通过 Discovery 动态获取，无需静态配置。完整参数列表见 `CONFIG.md`。

### Recall

与 vLLM 同容器部署，安装 vLLM wheel 包和 Qwen3-0.6B 模型文件。暴露端口 8002（brpc）和 8000（vLLM）。

启动流程：后台启动 vLLM → 轮询 health 等待就绪（最长 120s）→ 启动 recall_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 配置级别 | 说明 |
|---------|--------|---------|------|
| `SERVER_PORT` | 8002 | 允许调整 | 监听端口 |
| `VLLM_PORT` | 8000 | 允许调整 | vLLM 端口 |
| `VLLM_ENDPOINT` | /v1/chat/completions | 允许调整 | vLLM 接口路径 |
| `MODEL_NAME` | /app/models/Qwen3-0.6B/ | 允许调整 | 模型路径（容器内） |
| `VLLM_TIMEOUT_MS` | 100000 | 允许调整 | vLLM 请求超时 |
| `SKU_COUNT` | 100 | 允许调整 | SKU 数量 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 允许调整 | 服务发现地址 |

### Precalc

编译 `precalc_server`，将用户特征预计算结果写入 KVWorker。

启动流程：启动 precalc_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 配置级别 | 说明 |
|---------|--------|---------|------|
| `SERVER_PORT` | 8003 | 允许调整 | 监听端口 |
| `PRECALC_KVWORKER_HOST` | — | 必须指定 | KVWorker 地址 |
| `PRECALC_KVWORKER_PORT` | — | 必须指定 | KVWorker 端口 |
| `PRECALC_ETCD_ADDRESS` | — | 必须指定 | ETCD 地址 |
| `TTL_SECONDS` | 5 | 允许调整 | KV 缓存 TTL |
| `PRECALC_RESULT_SIZE_MB` | 8.5 | 允许调整 | 预计算结果大小 (MB) |
| `PAYLOAD_SIZE_KB` | 100 | 允许调整 | Payload 大小 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 允许调整 | 服务发现地址 |

### RankMaster

编译 `rank_master_server`，将候选商品分发给多个 RankSub 并行打分后归并。

启动流程：等待 RankSub 就绪（最长 120s）→ 启动 rank_master_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 配置级别 | 说明 |
|---------|--------|---------|------|
| `SERVER_PORT` | 8004 | 允许调整 | 监听端口 |
| `SUB_WORKER_COUNT` | 3 | 允许调整 | RankSub 数量 |
| `SUB_WORKER_ADDRESSES` | rank-sub-service:8005 | 允许调整 | RankSub 地址 |
| `TOP_K` | 100 | 允许调整 | 返回 Top-K |
| `DISCOVERY_ADDR` | discovery-server:8100 | 允许调整 | 服务发现地址 |

### RankSub

编译 `rank_sub_server`，从 KVWorker 读取特征 tensor 并对分配到的 SKU 打分。

启动流程：启动 rank_sub_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 配置级别 | 说明 |
|---------|--------|---------|------|
| `SERVER_PORT` | 8005 | 允许调整 | 监听端口 |
| `RANKSUB_KVWORKER_HOST` | — | 必须指定 | KVWorker 地址 |
| `RANKSUB_KVWORKER_PORT` | — | 必须指定 | KVWorker 端口 |
| `RANKSUB_ETCD_ADDRESS` | — | 必须指定 | ETCD 地址 |
| `SCORING_DELAY_MS` | 100 | 允许调整 | 打分延迟 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 允许调整 | 服务发现地址 |

### Feature

编译 `feature_server`（mock），当前为桩实现。

启动流程：启动 feature_server → 启动 discovery_client 注册。

| 环境变量 | 默认值 | 配置级别 | 说明 |
|---------|--------|---------|------|
| `SERVER_PORT` | 8001 | 允许调整 | 监听端口 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 允许调整 | 服务发现地址 |

### KVWorker

安装 `openyuanrong_datasystem` 包，运行 `start_datasystem.sh`。

启动流程：执行 start_datasystem.sh。

无环境变量配置。
