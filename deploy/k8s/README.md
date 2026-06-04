# Kubernetes 部署指南

## 环境准备

### 1. 安装 K8s 集群

以下是单节点集群（适合开发测试）的快速部署方式。生产环境建议使用多节点集群。

#### 使用 kubeadm（推荐）

```bash
# 1. 所有节点：关闭 swap
sudo swapoff -a
sudo sed -i '/swap/d' /etc/fstab

# 2. 所有节点：加载内核模块
cat <<EOF | sudo tee /etc/modules-load.d/k8s.conf
overlay
br_netfilter
EOF
sudo modprobe overlay
sudo modprobe br_netfilter
cat <<EOF | sudo tee /etc/sysctl.d/k8s.conf
net.bridge.bridge-nf-call-iptables  = 1
net.bridge.bridge-nf-call-ip6tables = 1
net.ipv4.ip_forward                 = 1
EOF
sudo sysctl --system

# 3. 所有节点：安装 containerd
sudo apt-get update
sudo apt-get install -y containerd
sudo mkdir -p /etc/containerd
containerd config default | sudo tee /etc/containerd/config.toml
sudo systemctl restart containerd

# 4. 所有节点：安装 kubeadm、kubelet、kubectl
sudo apt-get install -y apt-transport-https ca-certificates curl
curl -fsSL https://pkgs.k8s.io/core:/stable:/v1.28/deb/Release.key | sudo gpg --dearmor -o /etc/apt/keyrings/kubernetes-apt-keyring.gpg
echo 'deb [signed-by=/etc/apt/keyrings/kubernetes-apt-keyring.gpg] https://pkgs.k8s.io/core:/stable:/v1.28/deb/ /' | sudo tee /etc/apt/sources.list.d/kubernetes.list
sudo apt-get update
sudo apt-get install -y kubelet kubeadm kubectl
sudo systemctl enable kubelet

# 5. Master 节点：初始化集群
sudo kubeadm init --pod-network-cidr=10.244.0.0/16

# 6. Master 节点：配置 kubectl
mkdir -p $HOME/.kube
sudo cp /etc/kubernetes/admin.conf $HOME/.kube/config
sudo chown $(id -u):$(id -g) $HOME/.kube/config

# 7. Master 节点：安装网络插件（Calico）
kubectl apply -f https://raw.githubusercontent.com/projectcalico/calico/v3.26.1/manifests/calico.yaml

# 8. 单节点集群：允许 Master 节点调度 Pod
kubectl taint nodes --all node-role.kubernetes.io/control-plane-
```

#### 使用 minikube（本地开发）

```bash
curl -LO https://storage.googleapis.com/minikube/releases/latest/minikube-linux-amd64
sudo install minikube-linux-amd64 /usr/local/bin/minikube
minikube start --driver=docker --gpus all --cpus=4 --memory=16g
minikube kubectl -- get pods -A
alias kubectl="minikube kubectl --"
```

### 2. 配置 kubectl

```bash
kubectl config current-context
kubectl cluster-info
kubectl get nodes
```

如果使用远程集群：

```bash
scp user@master-ip:/etc/kubernetes/admin.conf ~/.kube/config
```

### 3. 配置 GPU 节点（Recall 服务需要）

```bash
kubectl get nodes -o=custom-columns=NAME:.metadata.name,GPU:.status.allocatable.nvidia\\.com/gpu

# 安装 NVIDIA GPU Operator
kubectl apply -f https://raw.githubusercontent.com/NVIDIA/gpu-operator/v23.9.0/deployments/gpu-operator/gpu-operator.yaml
kubectl wait --for=condition=ready pod -l app=nvidia-device-plugin-daemonset -n gpu-operator --timeout=300s
```

### 4. 准备镜像

```bash
docker build -f deploy/docker/etcd/Dockerfile -t linquickrec/etcd:latest .
docker build -f deploy/docker/kv_worker/Dockerfile -t linquickrec/kv-worker:latest .
docker build -f deploy/docker/feature/Dockerfile -t linquickrec/feature:latest .
docker build -f deploy/docker/recall/Dockerfile -t linquickrec/recall:latest .
docker build -f deploy/docker/precalc/Dockerfile -t linquickrec/precalc:latest .
docker build -f deploy/docker/rank-master/Dockerfile -t linquickrec/rank-master:latest .
docker build -f deploy/docker/rank-sub/Dockerfile -t linquickrec/rank-sub:latest .
docker build -f deploy/docker/proxy/Dockerfile -t linquickrec/proxy:latest .

# minikube 环境：加载到 minikube Docker
minikube image load linquickrec/etcd:latest
minikube image load linquickrec/kv-worker:latest
# ... 其他镜像同理

# 远程集群：推送到镜像仓库
docker tag linquickrec/recall:latest <registry>/linquickrec/recall:latest
docker push <registry>/linquickrec/recall:latest
```

> Discovery 服务镜像需单独构建：`docker build -f services/discovery/Dockerfile -t linquickrec/discovery:latest .`

## 集群结构

```
namespace: linquickrec
├── ConfigMap: linquickrec-config              # 共享配置
├── StatefulSet: etcd (5 Pods)                 # etcd 集群，端口 2379/2380
├── Deployment: kv-worker (1 Pod)              # KV Worker（元戎 Datasystem）
├── Deployment: discovery-server (1 Pod)        # 服务发现中心（BRPC 模式备用）
├── Deployment: feature-service (1 Pod)         # 特征服务 (Mock)
├── Deployment: proxy-service (1 Pod)           # 网关服务
├── Deployment: recall-service (1 Pod)          # 召回服务，需要 GPU
├── Deployment: precalc-service (1 Pod)         # 前置计算服务
├── Deployment: rank-master-service (1 Pod)     # 精排主图服务
└── Deployment: rank-sub-service (3 Pods)       # 精排子图服务，可水平扩展

> etcd 集群部署在 K8s 集群内（5 副本 StatefulSet）。
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

每个服务镜像的 `ENTRYPOINT ["/app/entrypoint.sh"]` 自动完成服务注册：

1. 启动主服务进程（如 `feature_server`）
2. 根据 `REGISTRY_BACKEND` 环境变量启动对应的 `discovery_client`，向 etcd 或 discovery-server 注册本服务并维持心跳

Pod 内只有一个容器（主容器），无需额外的 sidecar。entrypoint.sh 会根据 `REGISTRY_BACKEND` 自动选择注册后端。

### 后端选择

通过 ConfigMap 中的 `REGISTRY_BACKEND` 切换服务发现后端，两种模式互斥：

**etcd 模式**（`REGISTRY_BACKEND=etcd`，默认）：

```bash
kubectl apply -f 00-namespace.yaml
kubectl apply -f 01-configmap.yaml
# etcd 在集群内部署
kubectl apply -f 02-etcd.yaml
kubectl apply -f 04-kv-worker.yaml
# 跳过 03-discovery.yaml，etcd 模式使用宿主机外部 etcd
kubectl apply -f 05-feature.yaml
kubectl apply -f 06-proxy.yaml
kubectl apply -f 07-recall.yaml
kubectl apply -f 08-precalc.yaml
kubectl apply -f 09-rank-master.yaml
kubectl apply -f 10-rank-sub.yaml
```

**discovery_server 模式**（`REGISTRY_BACKEND=discovery_server`）：

```bash
kubectl apply -f 00-namespace.yaml
kubectl apply -f 01-configmap.yaml
kubectl apply -f 04-kv-worker.yaml
kubectl apply -f 03-discovery.yaml     # 部署 discovery-server
kubectl apply -f 05-feature.yaml
kubectl apply -f 06-proxy.yaml
kubectl apply -f 07-recall.yaml
kubectl apply -f 08-precalc.yaml
kubectl apply -f 09-rank-master.yaml
kubectl apply -f 10-rank-sub.yaml
```

> 只需修改 ConfigMap 中 `REGISTRY_BACKEND` 的值即可切换模式，容器 entrypoint.sh 自动适配，无需修改 Deployment。

### 文件说明

| 文件 | 资源 | 说明 |
|------|------|------|
| `00-namespace.yaml` | Namespace | 创建 `linquickrec` 命名空间 |
| `01-configmap.yaml` | ConfigMap | 共享环境变量（服务发现、超时、vLLM 配置等） |
| `02-etcd.yaml` | StatefulSet + Service | etcd 集群（5 副本），端口 2379/2380 |
| `04-kv-worker.yaml` | Deployment + Service | KV Worker（元戎 Datasystem），端口 31501/31502 |
| `03-discovery.yaml` | Deployment + Service | Discovery 服务发现中心（备用），端口 8100 |
| `05-feature.yaml` | Deployment + Service | Feature 特征服务 (Mock)，端口 8001 |
| `06-proxy.yaml` | Deployment + Service | Proxy 网关服务，端口 8080 |
| `07-recall.yaml` | Deployment + Service | Recall 召回服务，需要 GPU 节点，端口 8002 |
| `08-precalc.yaml` | Deployment + Service | Precalc 前置计算服务，端口 8003 |
| `09-rank-master.yaml` | Deployment + Service | RankMaster 精排主图服务，端口 8004 |
| `10-rank-sub.yaml` | Deployment + Service | RankSub 精排子图服务，端口 8005 |
| `deploy.sh` | 部署脚本 | 一键部署/删除，支持 etcd / discovery / delete 三个子命令 |

## 一键部署

```bash
cd deploy/k8s

# etcd 模式（默认）
./deploy.sh etcd

# discovery_server 模式
./deploy.sh discovery

# 删除所有资源
./deploy.sh delete
```

脚本会自动检查 `kubectl` 可用性和集群连通性，按正确顺序部署/删除所有资源。

### 手动部署

如果需要逐文件控制，可参照"后端选择"章节中的 `kubectl apply` 命令。`02-etcd.yaml` 和 `03-discovery.yaml` 互斥，不要同时应用。

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

### 删除

```bash
kubectl delete -f 10-rank-sub.yaml
kubectl delete -f 09-rank-master.yaml
kubectl delete -f 08-precalc.yaml
kubectl delete -f 07-recall.yaml
kubectl delete -f 06-proxy.yaml
kubectl delete -f 05-feature.yaml
kubectl delete -f 03-discovery.yaml
kubectl delete -f 04-kv-worker.yaml
kubectl delete -f 02-etcd.yaml
kubectl delete -f 01-configmap.yaml
kubectl delete -f 00-namespace.yaml
```

## 配置说明

所有服务的共享配置在 `01-configmap.yaml` 中，通过 `envFrom` 注入到每个容器。

| 配置项 | 值 | 使用者 |
|--------|-----|--------|
| `REGISTRY_BACKEND` | etcd | 所有服务 |
| `ETCD_ENDPOINTS` | etcd-client:2379 | 所有服务 (discovery_client + 服务进程) |
| `DISCOVERY_ADDR` | discovery-server:8100 | BRPC 模式备用 |
| `HOST_ID` | default-host | KV Worker, Precalc, RankSub（通过 Downward API 注入节点名） |
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

- **etcd 模式**：当前默认使用 etcd 做服务注册与发现。etcd 集群部署在 K8s 内（5 副本 StatefulSet），通过 `etcd-client` Service 对内提供访问。每个服务的 entrypoint.sh 自动启动 discovery_client 向 etcd 注册并维持心跳。切换为 discovery_server 模式需修改 ConfigMap 中 `REGISTRY_BACKEND` 为 `discovery_server`，并部署 `03-discovery.yaml`。
- **Recall 服务**：需要 GPU 节点，readinessProbe 初始等待 120 秒（vLLM 模型加载耗时）
- **RankSub 副本数**：默认 3，通过 `kubectl scale` 水平扩展，RankMaster 通过 etcd 服务发现自动感知
- **镜像版本**：当前使用 `linquickrec/xxx:latest`，生产环境建议使用具体版本号
- **日志存储**：各服务日志写入 `/var/log/linquickrec`，当前使用 emptyDir（Pod 重启后丢失），生产环境建议挂载持久卷
- **启动顺序**：etcd StatefulSet 从 etcd-0 到 etcd-4 逐个启动，全部 Ready 后再启动其他服务
- **KV Worker**：使用 hostIPC 和 privileged 模式，挂载 /dev/shm
