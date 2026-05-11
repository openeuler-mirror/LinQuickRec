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
# 安装 minikube
curl -LO https://storage.googleapis.com/minikube/releases/latest/minikube-linux-amd64
sudo install minikube-linux-amd64 /usr/local/bin/minikube

# 启动集群
minikube start --driver=docker --gpus all --cpus=4 --memory=16g

# 配置 kubectl（minikube 自动配置）
minikube kubectl -- get pods -A

# 或者创建别名
alias kubectl="minikube kubectl --"
```

### 2. 配置 kubectl

```bash
# 查看当前上下文
kubectl config current-context

# 查看集群信息
kubectl cluster-info

# 查看节点状态
kubectl get nodes

# 如果是多节点集群，查看节点详情
kubectl describe node <node-name>

# 测试 kubectl 连接
kubectl get namespaces
```

如果使用远程集群，需要将 kubeconfig 文件复制到本地：

```bash
# 从远程 Master 节点复制配置
scp user@master-ip:/etc/kubernetes/admin.conf ~/.kube/config

# 或者合并到现有配置
KUBECONFIG=~/.kube/config:/path/to/remote-config kubectl config view --flatten > ~/.kube/config-new
mv ~/.kube/config-new ~/.kube/config
```

### 3. 配置 GPU 节点（Recall 服务需要）

```bash
# 检查节点是否有 GPU 资源
kubectl get nodes -o=custom-columns=NAME:.metadata.name,GPU:.status.allocatable.nvidia\\.com/gpu

# 如果显示 <none>，需要安装 NVIDIA 设备插件
# 前提：节点已安装 NVIDIA 驱动和 nvidia-container-toolkit

# 安装 NVIDIA GPU Operator（推荐）
kubectl apply -f https://raw.githubusercontent.com/NVIDIA/gpu-operator/v23.9.0/deployments/gpu-operator/gpu-operator.yaml

# 等待安装完成
kubectl wait --for=condition=ready pod -l app=nvidia-device-plugin-daemonset -n gpu-operator --timeout=300s

# 再次检查
kubectl get nodes -o=custom-columns=NAME:.metadata.name,GPU:.status.allocatable.nvidia\\.com/gpu
# 应该显示类似：1
```

### 4. 准备镜像

```bash
# 构建镜像（在项目根目录执行）
docker build -f deploy/docker/feature/Dockerfile -t lingquickrec/feature:latest .
docker build -f deploy/docker/recall/Dockerfile -t lingquickrec/recall:latest .
docker build -f deploy/docker/precalc/Dockerfile -t lingquickrec/precalc:latest .
docker build -f deploy/docker/rank-master/Dockerfile -t lingquickrec/rank-master:latest .
docker build -f deploy/docker/rank-sub/Dockerfile -t lingquickrec/rank-sub:latest .
docker build -f deploy/docker/proxy/Dockerfile -t lingquickrec/proxy:latest .

# 如果使用 minikube，直接加载到 minikube 的 Docker 中
minikube image load lingquickrec/feature:latest
minikube image load lingquickrec/recall:latest
minikube image load lingquickrec/precalc:latest
minikube image load lingquickrec/rank-master:latest
minikube image load lingquickrec/rank-sub:latest
minikube image load lingquickrec/proxy:latest

# 如果使用远程集群，推送到镜像仓库
docker tag lingquickrec/recall:latest <registry>/lingquickrec/recall:latest
docker push <registry>/lingquickrec/recall:latest
# 然后修改 YAML 中的 image 为完整仓库地址
```

> Discovery 服务镜像需单独构建：`docker build -f services/discovery/Dockerfile -t lingquickrec/discovery:latest .`

## 集群结构

```
namespace: lingquickrec
├── ConfigMap: lingquickrec-config          # 共享配置
├── Deployment: discovery-server (1 Pod)    # 服务发现中心
├── Deployment: feature-service (1 Pod)     # 特征服务 (Mock)
├── Deployment: proxy-service (1 Pod)       # 网关服务
├── Deployment: recall-service (1 Pod)      # 召回服务，需要 GPU
├── Deployment: precalc-service (1 Pod)     # 前置计算服务
├── Deployment: rank-master-service (1 Pod) # 精排主图服务
└── Deployment: rank-sub-service (10 Pods)  # 精排子图服务，可水平扩展
```

### 调用关系

```
Client
  ↓
Proxy (8080)
  ├─ Stage 1: FeatureService (8003) ──→ 返回用户特征 (Mock)       ← 同步
  ├─ Stage 2: RecallService (8001) ──→ vLLM (8000) ──→ Qwen3-0.6B ← 并行
  │          PrecalcService (8004) ──→ RankKVWorker (31502)         ← 并行
  └─ Stage 3: RankMaster (8005) ──→ K8s 负载均衡 ──→ RankSub ×10 (8006) ──→ RankKVWorker (31502)
       ↓
  Proxy → Client
```

所有服务启动时向 Discovery Server 注册，并通过心跳维持在线状态。服务间通过 K8s ClusterIP Service 发现，无需映射端口到宿主机。RankMaster 创建 10 个 brpc Channel 连接 `rank-sub-service:8006`，由 kube-proxy 自动负载均衡到 10 个 RankSub Pod。

### 文件说明

| 文件 | 资源 | 说明 |
|------|------|------|
| `00-namespace.yaml` | Namespace | 创建 `lingquickrec` 命名空间 |
| `01-configmap.yaml` | ConfigMap | 共享环境变量（服务地址、KVWorker、超时、模型路径等） |
| `09-discovery.yaml` | Deployment + Service | Discovery 服务发现中心，端口 8100 |
| `09-feature.yaml` | Deployment + Service | Feature 特征服务 (Mock)，端口 8003 |
| `09-proxy.yaml` | Deployment + Service | Proxy 网关服务，端口 8080 |
| `10-recall.yaml` | Deployment + Service | Recall 服务，需要 GPU 节点，readinessProbe 120s |
| `11-precalc.yaml` | Deployment + Service | Precalc 服务，1 副本 |
| `12-rank-master.yaml` | Deployment + Service | RankMaster 服务，1 副本，配置 `SUB_WORKER_ADDRESSES` |
| `13-rank-sub.yaml` | Deployment + Service | RankSub 服务，10 副本 |

## 部署命令

### 前提条件

- K8s 集群已就绪，kubectl 已配置
- Recall 服务需要 GPU 节点（已安装 NVIDIA 设备插件）
- Docker 镜像已构建并推送到可访问的镜像仓库

### 一键部署

```bash
# 按编号顺序应用所有资源
kubectl apply -f 00-namespace.yaml
kubectl apply -f 01-configmap.yaml
kubectl apply -f 09-discovery.yaml
kubectl apply -f 09-feature.yaml
kubectl apply -f 09-proxy.yaml
kubectl apply -f 10-recall.yaml
kubectl apply -f 11-precalc.yaml
kubectl apply -f 12-rank-master.yaml
kubectl apply -f 13-rank-sub.yaml
```

### 查看状态

```bash
# 查看所有资源
kubectl get all -n lingquickrec

# 查看 Pod 状态
kubectl get pods -n lingquickrec -o wide

# 查看某个服务的日志
kubectl logs -f deployment/discovery-server -n lingquickrec
kubectl logs -f deployment/feature-service -n lingquickrec
kubectl logs -f deployment/proxy-service -n lingquickrec
kubectl logs -f deployment/recall-service -n lingquickrec
kubectl logs -f deployment/rank-master-service -n lingquickrec

# 查看所有 RankSub Pod 的日志
kubectl logs -f deployment/rank-sub-service -n lingquickrec --all-containers --max-log-requests=10

# 查看事件（排查启动问题）
kubectl get events -n lingquickrec --sort-by='.lastTimestamp'
```

### 扩缩容

```bash
# 调整 RankSub 副本数
kubectl scale deployment rank-sub-service --replicas=5 -n lingquickrec

# 同时更新 ConfigMap 中的 SUB_WORKER_COUNT
kubectl edit configmap lingquickrec-config -n lingquickrec
# 将 SUB_WORKER_COUNT 改为对应副本数
```

> 扩缩容 RankSub 后，需要重启 RankMaster 使其读取新的 `SUB_WORKER_COUNT`：
> `kubectl rollout restart deployment rank-master-service -n lingquickrec`

### 更新与回滚

```bash
# 更新镜像（触发滚动更新）
kubectl set image deployment/recall-service recall=lingquickrec/recall:v2 -n lingquickrec

# 查看滚动更新状态
kubectl rollout status deployment/recall-service -n lingquickrec

# 查看历史版本
kubectl rollout history deployment/recall-service -n lingquickrec

# 回滚到上一版本
kubectl rollout undo deployment/recall-service -n lingquickrec

# 回滚到指定版本
kubectl rollout undo deployment/recall-service --to-revision=2 -n lingquickrec
```

### 删除

```bash
# 按逆序删除
kubectl delete -f 13-rank-sub.yaml
kubectl delete -f 12-rank-master.yaml
kubectl delete -f 11-precalc.yaml
kubectl delete -f 10-recall.yaml
kubectl delete -f 09-proxy.yaml
kubectl delete -f 09-feature.yaml
kubectl delete -f 09-discovery.yaml
kubectl delete -f 01-configmap.yaml
kubectl delete -f 00-namespace.yaml
```

## 配置说明

所有服务的共享配置在 `01-configmap.yaml` 中，通过 `envFrom` 注入到每个容器。各服务额外的环境变量在其 Deployment 中单独设置。

| 配置项 | 值 | 使用者 |
|--------|-----|--------|
| `DISCOVERY_ADDR` | discovery-server:8100 | 所有服务 (discovery_client) |
| `FEATURE_SERVICE_ADDR` | feature-service:8003 | Proxy |
| `RECALL_SERVICE_ADDR` | recall-service:8001 | Proxy |
| `PRECALC_SERVICE_ADDR` | precalc-service:8004 | Proxy |
| `RANK_SERVICE_ADDR` | rank-master-service:8005 | Proxy |
| `KVWORKER_HOST` | 141.61.84.245 | Precalc, RankSub |
| `KVWORKER_PORT` | 31502 | Precalc, RankSub |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | Precalc, RankSub |
| `VLLM_BASE_URL` | http://127.0.0.1:8000 | Recall |
| `VLLM_ENDPOINT` | /v1/chat/completions | Recall |
| `MODEL_NAME` | /app/models/Qwen3-0.6B/ | Recall |
| `SUB_WORKER_COUNT` | 10 | RankMaster |
| `SUB_WORKER_TIMEOUT_MS` | 5000 | RankMaster |
| `TOP_K` | 100 | RankMaster |
| `SCORING_DELAY_MS` | 100 | RankSub |

修改 ConfigMap 后需要重启相关服务才能生效。

## 注意事项

- **FeatureService (Mock)**：当前为 Mock 实现，返回随机用户特征和 SKU 特征。替换为真实实现时，只需修改 `services/FeatureService/` 下的源码并重新构建镜像
- **Recall 服务**：需要 GPU 节点，readinessProbe 初始等待 120 秒（vLLM 模型加载耗时）
- **RankSub 扩容**：修改副本数后需同步更新 ConfigMap 中的 `SUB_WORKER_COUNT` 并重启 RankMaster
- **镜像版本**：当前使用 `lingquickrec/xxx:latest`，生产环境建议使用具体版本号
- **日志存储**：各服务日志写入 `/var/log/lingquickrec`，当前使用 emptyDir（Pod 重启后丢失），生产环境建议挂载持久卷
- **启动顺序**：Discovery Server 应最先启动，其他服务依赖它进行注册和心跳
