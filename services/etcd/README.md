# etcd 服务

## 模块简介

etcd 是一个分布式、可靠的键值存储系统，在本项目中作为服务发现的可选后端。当 `--registry_backend=etcd` 时，`discovery_client` 使用 etcd v3 lease + keep-alive 机制代替自建 discovery_server 进行服务注册与心跳维持。

本模块仅包含容器化镜像定义，无 C++ 代码。

## 目录结构

```
services/etcd/
└── README.md

deploy/docker/etcd/
└── Dockerfile
```

## 业务流程

### etcd 后端服务注册流程

```
   service container
   ┌──────────────────────────────┐
   │   main service (:8003)        │
   └──────────┬───────────────────┘
              │  TCP probe (ready?)
   ┌──────────▼───────────────────┐
   │   discovery_client            │
   │  ──── LeaseGrant(TTL) ────> │         etcd (:2379)
   │  ──── Put(key, val, lease)─> │  ┌──────────────────────────┐
   │  ──── keep-alive (bg thr)──> │  │ /linquickrec/services/  │
   │                              │  │   proxy/                │
   │  <─── Range(prefix) ─────── │  │   recall_service/       │
   └──────────────────────────────┘  │   rank_service/         │
                                     │   feature_service/      │
                                     └──────────────────────────┘
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

本服务无需编译，直接拉取官方镜像即可：

```bash
docker pull quay.io/coreos/etcd:v3.5
```

## 启动方式

### Docker Compose

```bash
cd deploy/docker/discovery/examples
docker compose -f docker-compose.etcd.yml up --build
```

### 直接运行

```bash
docker run -d --name etcd \
  -p 2379:2379 \
  quay.io/coreos/etcd:v3.5 \
  etcd --listen-client-urls=http://0.0.0.0:2379 \
       --advertise-client-urls=http://0.0.0.0:2379
```

## 容器搭建

```bash
docker build -t linquickrec/etcd:latest \
  -f deploy/docker/etcd/Dockerfile .
```

```bash
docker run -d --name etcd \
  -p 2379:2379 \
  linquickrec/etcd:latest
```
