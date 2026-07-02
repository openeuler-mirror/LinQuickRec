# Kubernetes 部署指南

## 前提

本指南假设你已具备可用的 Kubernetes 集群和所有容器镜像（构建方法见 [docker 构建指南](../docker/README.md)）。

## 集群结构

```
deploy/k8s/
├── base/                             # 必部署资源（Kustomize base）
│   ├── kustomization.yaml
│   ├── namespace.yaml
│   ├── configmap.yaml
│   ├── etcd-pv.yaml
│   ├── etcd.yaml                     # StatefulSet (5 Pods) + Headless Service
│   ├── kv-worker.yaml                # DaemonSet + Service
│   ├── feature.yaml                  # Deployment + Service (Mock)
│   ├── proxy.yaml                    # Deployment + Service
│   ├── precalc.yaml                  # Deployment + Service
│   ├── rank-master.yaml              # Deployment + Service
│   └── rank-sub.yaml                 # Deployment + Service (3 replicas)
├── components/                       # 可选 Component
│   ├── discovery/                    # discovery-server, 由 --discovery-backend 控制
│   ├── recall-vllm/                  # vLLM 模式 recall, 由 --recall-mode 控制
│   └── recall-novllm/                # novllm 模式 recall, 由 --recall-mode 控制
├── deploy.sh
└── README.md
```

### 调用关系

```
Client
  ↓
Proxy (:8080)
  ├─ Stage 1: FeatureService (:8001) ──→ 返回用户特征 (Mock)       ← 同步
  ├─ Stage 2: RecallService (:8002) ──→ vLLM (:8000) ──→ Qwen3-0.6B ← 并行
  │          PrecalcService (:8003) ──→ KVWorker (:31501/:31502)     ← 并行
  └─ Stage 3: RankMaster (:8004) ──→ 服务发现 ──→ RankSub ×3 (:8005) ──→ KVWorker (:31501/:31502)
       ↓
  Proxy → Client
```

### 服务发现机制

每个服务镜像的 entrypoint.sh 自动完成服务注册：

1. 启动主服务进程（如 `feature_server`）
2. 根据 `REGISTRY_BACKEND` 环境变量启动 discovery_client，向 etcd 或 discovery-server 注册本服务并维持心跳

### 文件说明

| 文件/目录 | 资源 | 说明 |
|-----------|------|------|
| `base/` | Kustomize base (10 个 YAML) | 必部署资源：namespace, configmap, etcd, kv-worker, feature, proxy, precalc, rank-master, rank-sub |
| `components/discovery/` | Component | 可选：discovery-server, 由 `--discovery-backend` 控制 |
| `components/recall-vllm/` | Component | 可选：vLLM 模式 recall，由 `--recall-mode` 控制 |
| `components/recall-novllm/` | Component | 可选：novllm 模式 recall，由 `--recall-mode` 控制 |
| `deploy.sh` | 部署脚本 | `start|stop|delete` 命令，支持 `--registry --recall-mode --discovery-backend` 参数 |

## 集群部署

### 脚本一键部署

`deploy.sh` 脚本默认使用novllm+etcd模式

```bash
cd deploy/k8s

# 启动（novllm + etcd，默认，使用运行时的本地镜像）
bash deploy.sh start

# 启动 + 私有 registry
bash deploy.sh start -r 192.168.0.1:5000

# 启动（vLLM + discovery_server + 私有 registry）
bash deploy.sh start --recall-mode vllm --discovery-backend discovery-server -r 192.168.0.1:5000
```

`start` 支持参数：`-r/--registry`（registry 地址，如 `192.168.0.1:5000`，不指定则用本地镜像）、`--recall-mode` (vllm/novllm)、`--discovery-backend` (etcd/discovery-server)。

**停止和删除：**

```
# 停止（缩容到 0，保留定义）
bash deploy.sh stop

# 删除所有资源
bash deploy.sh delete
```

### 手动部署

如需逐文件控制，可使用 Kustomize 直接部署：

```bash
# 仅 base 资源
kubectl apply -k base/

# base + discovery
kubectl apply -k components/discovery && kubectl apply -k base/
```

`base/` 和 `components/discovery` / `components/recall-vllm` 互斥（同资源名），不要同时应用。建议使用 `deploy.sh` 脚本自动处理。

### 查看状态

```bash
kubectl get all -n linquickrec
kubectl get pods -n linquickrec -o wide

# 查看 etcd 中注册的服务
kubectl exec -n linquickrec etcd-0 -- etcdctl get /linquickrec/services/ --prefix

# 查看日志
kubectl logs -f deployment/proxy-service -n linquickrec
kubectl logs -f deployment/recall-service -n linquickrec
kubectl logs -f deployment/feature-service -n linquickrec

# 查看事件
kubectl get events -n linquickrec --sort-by='.lastTimestamp'
```

### 扩缩容

```bash
# 调整 RankSub 副本数
kubectl scale deployment rank-sub-service --replicas=10 -n linquickrec

# 调整其他服务（支持多副本的服务）
kubectl scale deployment feature-service --replicas=3 -n linquickrec
kubectl scale deployment precalc-service --replicas=3 -n linquickrec

# 如需压测 Proxy Service 入口的负载均衡，先扩容 Proxy
kubectl scale deployment proxy-service --replicas=3 -n linquickrec
```

> 服务通过 etcd 服务发现动态感知副本变化，无需额外配置修改。

### Proxy 并发压测

```bash
# 从本机通过 Kubernetes API service proxy 访问 proxy-service
# 请求进入 K8s Service，由 Service 转发到后端 Proxy Pod
bash scripts/send_proxy_request.sh --k8s -n 1000 -c 50

# 指定命名空间、Service 名或 Service 端口
bash scripts/send_proxy_request.sh --k8s --namespace=linquickrec --service=proxy-service --service-port=8080 -n 1000 -c 50

# 如果直接暴露了 NodePort/LoadBalancer，也可以用直连模式
bash scripts/send_proxy_request.sh --url=http://<node-or-lb>:8080/Proxy/Recommend -n 1000 -c 50
```

`kubectl port-forward` 更适合临时调试，通常会固定转发到某一个 Pod；多副本压测建议使用上面的 `--k8s` 模式或直接访问 NodePort/LoadBalancer 的 Service 入口。

### 更新与回滚

```bash
kubectl set image deployment/recall-service recall=linquickrec/recall:v2 -n linquickrec
kubectl rollout status deployment/recall-service -n linquickrec
kubectl rollout history deployment/recall-service -n linquickrec
kubectl rollout undo deployment/recall-service -n linquickrec
```

## 配置说明

### 服务发现后端选择

通过 `REGISTRY_BACKEND` 切换服务发现后端，两种模式互斥：

**etcd 模式**（默认）：`--discovery-backend=etcd`

```bash
bash deploy.sh start --discovery-backend etcd
```

**discovery_server 模式**：`--discovery-backend=discovery-server`

```bash
bash deploy.sh start --discovery-backend discovery-server
```

> 只需修改 deploy.sh 的 `--discovery-backend` 参数即可切换模式，容器 entrypoint.sh 自动适配，无需修改 Deployment。

### recall-vllm 与 novllm 选用

Recall 服务有两种部署模式：

- **vLLM 模式**：需要 GPU 节点，容器内启动 vLLM 推理服务，通过 localhost HTTP 调用。适用于需要真实大模型召回的场景。
- **novllm 模式**：无需 GPU，使用 KVCache 模拟召回，通过 KVWorker 读写缓存。适用于压测或无 GPU 环境。

选用方式：通过 `--recall-mode` 参数选择，两种模式共用相同镜像。

所有服务的共享配置在 ConfigMap 中，通过 `envFrom` 注入到每个容器。

| 配置项 | 值 | 使用者 |
|--------|-----|--------|
| `REGISTRY_BACKEND` | etcd | 所有服务 |
| `ETCD_ENDPOINTS` | etcd-client:2379 | 所有服务 (discovery_client + 服务进程) |
| `DISCOVERY_ADDR` | discovery-server:8100 | BRPC 模式备用 |
| `HOST_ID` | default-host | KV Worker, Precalc, Recall novllm, RankSub（通过 Downward API 注入节点名） |
| `FEATURE_SERVICE_NAME` | feature_service | Proxy |
| `RECALL_SERVICE_NAME` | recall_service | Proxy |
| `PRECALC_SERVICE_NAME` | precalc_service | Proxy |
| `RANK_SERVICE_NAME` | rank_service | Proxy |
| `SUB_WORKER_SERVICE_TYPE` | rank_sub | RankMaster |
| `KV_WORKER_SERVICE` | kv_worker | Precalc, RankSub |
| `HEARTBEAT_INTERVAL` | 5 | 所有服务 (discovery_client) |
| `VLLM_BASE_URL` | http://127.0.0.1:8000 | Recall |
| `MODEL_NAME` | /app/models/Qwen3-0.6B/ | Recall |
| `SUB_WORKER_PARALLELISM` | 4 | RankMaster |
| `TOP_K` | 100 | RankMaster |
| `FEATURE_SLEEP_TIME_MS` | 30 | Feature |
| `RECALL_SLEEP_TIME_MS` | 30 | Recall |
| `KVCACHE_HIT_RATE` | 0.5 | Recall novllm |
| `KVCACHE_HIT_SLEEP_TIME_MS` | 10 | Recall novllm |
| `KVCACHE_MISS_SLEEP_TIME_MS` | 100 | Recall novllm |
| `PRECALC_SLEEP_TIME_MS` | 30 | Precalc |
| `RANK_MASTER_SLEEP_TIME_MS` | 30 | RankMaster |
| `RANK_SUB_SLEEP_TIME_MS` | 30 | RankSub |
| `FEATURE_PAYLOAD_SIZE_KB` | 0 | Feature |
| `RECALL_PAYLOAD_SIZE_KB` | 0 | Recall |
| `PAYLOAD_SIZE_KB` | 100 | Precalc |
| `RANK_MASTER_PAYLOAD_SIZE_KB` | 0 | RankMaster |
| `RANK_SUB_PAYLOAD_SIZE_KB` | 0 | RankSub |
| `TTL_SECONDS` | 5 | Precalc |

修改 ConfigMap 后需要重启相关服务才能生效。

## 注意事项

- **etcd 模式**：当前默认使用 etcd 做服务注册与发现。etcd 集群部署在 K8s 内（5 副本 StatefulSet），通过 `etcd-client` Service 对内提供访问。每个服务的 entrypoint.sh 自动启动 discovery_client 向 etcd 注册并维持心跳。切换为 discovery_server 模式需使用 `--discovery-backend discovery-server` 参数。
- **Recall 服务**：vLLM 版本需要 GPU 节点，readinessProbe 初始等待 120 秒（vLLM 模型加载耗时）；novllm 模式（`--recall-mode novllm`）使用 KVClient，需要 `hostIPC`、`privileged` 和宿主机 `/dev/shm`
- **RankSub 副本数**：默认 3，通过 `kubectl scale` 水平扩展，RankMaster 通过 etcd 服务发现自动感知
- **镜像版本**：当前使用 `linquickrec/xxx:latest`，生产环境建议使用具体版本号
- **日志存储**：各服务日志写入 `/var/log/linquickrec`，当前使用 emptyDir（Pod 重启后丢失），生产环境建议挂载持久卷
- **启动顺序**：etcd StatefulSet 从 etcd-0 到 etcd-4 逐个启动，全部 Ready 后再启动其他服务
- **KV Worker**：使用 hostIPC 和 privileged 模式，挂载 /dev/shm
