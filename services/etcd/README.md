# etcd 服务

## 模块简介

etcd 是一个分布式、可靠的键值存储系统，在本项目中作为服务发现的可选后端。当 `--registry_backend=etcd` 时，`discovery_client` 使用 etcd v3 lease + keep-alive 机制代替自建 discovery_server 进行服务注册与心跳维持。

本模块仅包含容器化镜像定义，无 C++ 代码。

集群数据存储在 emptyDir 中，Pod 重启时通过 Raft 从 peer 同步恢复。

## 目录结构

```
services/etcd/
├── README.md
└── ...

deploy/docker/etcd/
├── Dockerfile
└── entrypoint.sh

deploy/k8s/base/
├── etcd-headless-svc.yaml            # Headless Service (Pod peer DNS)
├── etcd-client-svc.yaml              # ClusterIP Service (client access)
└── etcd-statefulset.yaml             # StatefulSet (5 Pods, emptyDir)
```

## 业务流程

### etcd 后端服务注册流程

```
   service container
   ┌──────────────────────────────┐
   │   main service (:8003)       │
   └──────────┬───────────────────┘
              │  TCP probe (ready?)
   ┌──────────▼───────────────────┐
   │   discovery_client           │
   │  ──── LeaseGrant(TTL) ─────> │         etcd (:2379)
   │  ──── Put(key, val, lease)─> │  ┌─────────────────────────┐
   │  ──── keep-alive (bg thr)──> │  │ /linquickrec/services/  │
   │                              │  │   proxy/                │
   │  <─── Range(prefix) ──────── │  │   recall_service/       │
   └──────────────────────────────┘  │   rank_service/         │
                                     │   feature_service/      │
                                     └─────────────────────────┘
```

etcd 自动通过 lease 过期机制清理崩溃实例，无需额外的健康检查服务端。

### 核心机制

| 操作 | etcd API | 说明 |
|------|----------|------|
| 注册 | `LeaseGrant` + `Put` | 创建 TTL 租约并写入 key |
| 心跳 | `LeaseKeepAlive` | 客户端后台线程自动续约 |
| 反注册 | `LeaseRevoke` / `DeleteRange` | 关闭时主动清理 |
| 发现 | `Range` (prefix) | 查询存活的所有 key |

### Key 命名规范

```
/linquickrec/services/{service_name}/{instance_id}
```

Value 格式：`{"host":"10.0.0.5","port":8001}`

## 编译命令

本服务无需编译，直接使用构建好的linquickrec/etcd:latest镜像即可。

## 启动方式

### 启动命令

```bash
docker run -d --name etcd \
  -p 2379:2379 \
  linquickrec/etcd:latest \
  etcd --listen-client-urls=http://0.0.0.0:2379 \
       --advertise-client-urls=http://0.0.0.0:2379
```

### 配置参数

etcd 通过环境变量配置：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `CLUSTER_SIZE` | `5` | 集群节点数（需与 StatefulSet replicas 一致） |
| `SERVICE_NAME` | `etcd` | Headless Service 名 |
| `CLUSTER_NS` | `linquickrec` | K8s 命名空间 |
| `DATA_DIR` | `/var/lib/etcd` | etcd 数据目录（PVC 持久化，Pod 重启后保留） |

## 容器搭建

### 构建镜像

```bash
docker build -t linquickrec/etcd:latest \
  -f deploy/docker/etcd/Dockerfile .
```

### 运行容器

```bash
docker run -d --name etcd \
  -p 2379:2379 \
  linquickrec/etcd:latest
```

## 测试方法

### 集群健康检查

**Pod 内：**

```bash
kubectl exec -n linquickrec etcd-0 -- etcdctl endpoint status --cluster -w table
kubectl exec -n linquickrec etcd-0 -- etcdctl member list -w table
```

**主机：**

```bash
curl -s http://<任一节点IP>:32379/health
```

etcd-client Service 已暴露为 NodePort 32379，可从集群外直接访问。

预期输出（5 个成员全部 IS_HEALTHY=true，有一个 LEADER）：

```text
+---------------------------+---------+--------+---------+--------+-----------+
|         ENDPOINT          |   ID    |VERSION | DB SIZE | LEADER |IS_HEALTHY |
+---------------------------+---------+--------+---------+--------+-----------+
| etcd-0.etcd.linquickrec.. | c204... | 3.5.12 |  20 kB  |  false |     true  |
| etcd-1.etcd.linquickrec.. | 1904... | 3.5.12 |  20 kB  |  true  |     true  |
| etcd-2.etcd.linquickrec.. | 3d9e... | 3.5.12 |  20 kB  |  false |     true  |
| etcd-3.etcd.linquickrec.. | 2a02... | 3.5.12 |  20 kB  |  false |     true  |
| etcd-4.etcd.linquickrec.. | 3a94... | 3.5.12 |  20 kB  |  false |     true  |
+---------------------------+---------+--------+---------+--------+-----------+
```

### 服务注册检查

**Pod 内：**

```bash
kubectl exec -n linquickrec etcd-0 -- etcdctl get /linquickrec/services/ --prefix --keys-only
```

**主机：**

```bash
etcdctl --endpoints=http://<任一节点IP>:32379 get /linquickrec/services/ --prefix --keys-only
```

（需本地安装 etcdctl：`apt install etcd-client` 或从 GitHub releases 下载二进制。）

预期输出（7 个业务服务注册成功）：

```text
/linquickrec/services/proxy/proxy_192.168.219.77_8080
/linquickrec/services/recall_service/recall_service_192.168.219.77_8002
/linquickrec/services/feature_service/feature_service_192.168.219.77_8001
/linquickrec/services/precalc_service/precalc_service_192.168.219.77_8003
/linquickrec/services/rank_service/rank_service_192.168.219.77_8004
/linquickrec/services/rank_sub/rank_sub_192.168.219.77_8005
/linquickrec/services/kv_worker/kv_worker_192.168.219.77_31501
```

### 实时监听

**Pod 内：**

```bash
kubectl exec -n linquickrec etcd-0 -- etcdctl watch /linquickrec/services/ --prefix
```

观察 Pod 重启或扩缩容时 key 的动态变化。
