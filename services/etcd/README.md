# etcd 服务

## 模块简介

etcd 是一个分布式、可靠的键值存储系统，在本项目中作为服务发现的可选后端。当 `--registry_backend=etcd` 时，`discovery_client` 使用 etcd v3 lease + keep-alive 机制代替自建 discovery_server 进行服务注册与心跳维持。

本模块仅包含容器化镜像定义，无 C++ 代码。

## 目录结构

```
services/etcd/
├── README.md
└── ...

deploy/docker/etcd/
├── Dockerfile
└── entrypoint.sh

deploy/k8s/base/
└── etcd.yaml                        # StatefulSet (5 Pods) + 2 Services
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
| `DATA_DIR` | `/var/lib/etcd` | 数据目录 |
| `CLUSTER_SIZE` | `5` | 集群节点数 |
| `SERVICE_NAME` | `etcd` | Headless Service 名 |
| `CLUSTER_NS` | `linquickrec` | K8s 命名空间 |
| `SNAPSHOT_COUNT` | `5000` | 快照计数阈值 |

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

### 集群健康

```bash
# 成员列表（应为 5 个 started 的 etcd 节点）
kubectl exec -n linquickrec etcd-0 -- etcdctl member list -w table

# 端点状态（所有成员 IS_HEALTHY=true）
kubectl exec -n linquickrec etcd-0 -- etcdctl endpoint status --cluster -w table
```

预期输出：

```
+------------------+---------+-------+---------+--------+-----------+
|       ENDPOINT   |    ID   |VERSION|DB SIZE  |LEADER  |IS_HEALTHY |
+------------------+---------+-------+---------+--------+-----------+
| etcd-0.etcd...   | c204... | 3.5.12| 20 kB   |  true  |     true  |
| etcd-1.etcd...   | 1904... | 3.5.12| 20 kB   |  false |     true  |
| ...                                                  |     true  |
+------------------+---------+-------+---------+--------+-----------+
```

### 读写验证

```bash
# etcd-0 写入
kubectl exec -n linquickrec etcd-0 -- etcdctl put test-key "hello"

# etcd-1 读取（验证数据复制）
kubectl exec -n linquickrec etcd-1 -- etcdctl get test-key

# 清理
kubectl exec -n linquickrec etcd-0 -- etcdctl del test-key
```

### 服务注册

```bash
# 列出所有已注册的 discovery_client key
kubectl exec -n linquickrec etcd-0 -- etcdctl get /linquickrec/ --prefix --keys-only

# 查看注册详情（host + port）
kubectl exec -n linquickrec etcd-0 -- etcdctl get /linquickrec/ --prefix
```

正常部署后应包含 `proxy`、`recall_service`、`feature_service`、`precalc_service`、`rank_service`、`rank_sub` 等服务的注册信息。

### 实时监听

```bash
# 监听注册变化（部署/停止服务时观察 key 动态）
kubectl exec -n linquickrec etcd-0 -- etcdctl watch /linquickrec/ --prefix
```
