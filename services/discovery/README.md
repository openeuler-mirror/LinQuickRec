# 服务发现中心 — Service Discovery

## 模块简介

服务发现中心提供了一套独立于业务代码的服务注册与发现机制，解决容器化部署场景下服务实例地址动态变化的难题。

本模块采用**双后端架构**，通过 `--registry_backend` 启动参数动态选择服务注册中心：

- **discovery_server**（内置 BRPC 服务端）— 默认后端，维持原有逻辑
- **etcd**（etcd v3 集群）— 基于 etcd lease + keep-alive 机制，无需额外运行 discovery_server

本模块包含两个可执行文件：

- **discovery_server** — 服务发现中心服务端（仅 discovery_server 后端需要），运行在独立容器中，维护注册表并执行健康检查
- **discovery_client** — 独立的辅助进程，支持两种后端，部署在每个业务服务容器中，负责向注册中心注册本实例并周期发送心跳

模块采用零侵入式设计，业务服务无需修改任何代码即可接入服务发现。

### 架构抽象

注册端和发现端各自定义了策略接口，由 `--registry_backend` 动态选择实现：

```
discovery_client (main.cpp)
  └── IRegistryBackend  ← 注册/心跳/注销
       ├── BrpcRegistryBackend    (discovery_server 后端)
       └── EtcdRegistryBackend    (etcd 后端)

消费者 (proxy / rank_master)
  └── IDiscoveryProvider ← 服务发现
       ├── BrpcDiscoveryProvider  (discovery_server 后端)
       └── EtcdDiscoveryProvider  (etcd 后端)
```

## 目录结构

```
services/discovery/
├── CMakeLists.txt
├── DESIGN.md
├── README.md
├── build.sh
├── client/
│   ├── CMakeLists.txt              # discovery_client_lib 静态库
│   ├── include/
│   │   ├── registry_backend.h      # IRegistryBackend 抽象接口
│   │   ├── discovery_provider.h    # IDiscoveryProvider 抽象接口
│   │   ├── brpc_registry_backend.h # BRPC 注册后端实现
│   │   ├── brpc_discovery_provider.h
│   │   ├── etcd_registry_backend.h # etcd 注册后端实现
│   │   ├── etcd_discovery_provider.h
│   │   └── etcd_client.h           # etcd v3 HTTP API 封装
│   └── src/
│       ├── main.cpp                # discovery_client 入口
│       ├── registry_backend_factory.cpp
│       ├── discovery_provider_factory.cpp
│       ├── brpc_registry_backend.cpp
│       ├── brpc_discovery_provider.cpp
│       ├── etcd_client.cpp         # etcd v3 REST API 客户端
│       ├── etcd_http.cpp           # 轻量 raw socket HTTP POST
│       ├── etcd_registry_backend.cpp
│       ├── etcd_discovery_provider.cpp
│       ├── simple_json.h / .cpp   # 最小化 JSON 解析/构建
│       └── etcd_http.h
├── server/
│   ├── include/
│   │   └── discovery_server.h      # DiscoveryServiceImpl
│   └── src/
│       ├── main.cpp
│       └── discovery_server.cpp
└── examples/
    ├── CMakeLists.txt
    ├── README.md
    ├── docker-compose.yml
    ├── pseudo_service/
    │   └── main.cpp
    └── tests/
        ├── test_discover.cpp
        ├── test_heartbeat_cycle.cpp
        └── test_register.cpp
```

## 业务流程

### 后端对比

| 特性 | discovery_server | etcd |
|------|------------------|------|
| 注册方式 | BRPC Register RPC | etcd LeaseGrant + Put |
| 心跳方式 | BRPC Heartbeat RPC（客户端主动调用） | etcd LeaseKeepAlive（后台线程自动续约） |
| 清理机制 | 服务端 health_check_loop 超时清理 | etcd lease 过期自动删除 key |
| instance_id 来源 | 服务端生成（带 monotonic counter） | 客户端本地生成 |
| 额外进程 | 需要运行 discovery_server | 需要运行 etcd 集群 |
| 配置参数 | `--discovery_addr` | `--etcd_endpoints` |

### 服务注册流程 (discovery_server 后端)

```
        service container
   ┌──────────────────────────────┐
   │   main service (:8003)        │
   └──────────┬───────────────────┘
              │  TCP probe (ready?)
   ┌──────────▼───────────────────┐
   │    discovery_client           │
   │  ──── Register ───────────>  │  discovery_server (:8100)
   │  ──── heartbeat every 5s ──> │
   │  <─── Discover ───────────── │
   │  ──── Deregister on stop ──> │
   └──────────────────────────────┘
```

### 服务注册流程 (etcd 后端)

```
        service container
   ┌──────────────────────────────┐
   │   main service (:8003)        │
   └──────────┬───────────────────┘
              │  TCP probe (ready?)
   ┌──────────▼───────────────────┐
   │    discovery_client           │
   │  ──── LeaseGrant(TTL) ─────> │         etcd
   │  ──── Put(key, val, lease)─> │  ┌──────────────────┐
   │  ──── keep-alive (bg thr)──> │  │ /linquickrec/    │
   │                               │  │   services/      │
   │  <─── Range(prefix) ──────── │  │     recall/      │
   └──────────────────────────────┘  │       instance1/ │
                                     │       instance2/ │
                                     └──────────────────┘
```

### etcd Key 命名规范

```
/linquickrec/services/{service_name}/{instance_id}
```

Value (JSON):

```json
{"host": "10.0.0.5", "port": 8001}
```

### 状态转换

```
   Register
     │
     ▼
┌────────┐     heartbeat timeout     ┌────────┐     cleanup timeout    ┌─────────┐
│  UP    │ ────────────────────────> │  DOWN  │ ─────────────────────> │ REMOVED │
└────────┘                           └────────┘                        └─────────┘
    │
    └── Deregister ──> REMOVED
```

discovery_server 后端由服务端 passive 检测实现状态转移；etcd 后端由 lease 过期自动实现 key 删除。

### 核心交互

| 操作 | 后端 | 协议 | 说明 |
|------|------|------|------|
| Register | discovery_server | BRPC | 注册本实例（service_type, host, port） |
| Register | etcd | HTTP (etcd v3 REST) | LeaseGrant + Put key |
| Heartbeat | discovery_server | BRPC | 周期性心跳，重置超时计时器 |
| Heartbeat | etcd | HTTP (etcd v3 REST) | LeaseKeepAlive（后台线程自动） |
| Deregister | discovery_server | BRPC | 优雅退出时主动反注册 |
| Deregister | etcd | HTTP (etcd v3 REST) | 停止 keep-alive + Delete key |
| Discover | discovery_server | BRPC | 查询指定 service_type 的 UP 实例列表 |
| Discover | etcd | HTTP (etcd v3 REST) | Range prefix 查询存活的 key |

## 编译命令

| 依赖 | 版本要求 | 备注 |
|------|----------|------|
| CMake | >= 3.14 | 编译工具链 |
| brpc | >= 1.4 | `linquickrec/base:latest` 基础镜像已内置 |
| protobuf | >= 3.0 | `linquickrec/base:latest` 基础镜像已内置 |
| abseil-cpp | latest | `linquickrec/base:latest` 基础镜像已内置 |

### 脚本构建

```bash
cd services/discovery
./build.sh              # 默认为 Release 构建
./build.sh release      # Release 构建
./build.sh debug        # Debug 构建
./build.sh clean        # 清理后构建
```

### 手动构建

```bash
cd services/discovery
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make discovery_server discovery_client -j$(nproc)
```

### 编译产物

| 二进制 | 说明 |
|--------|------|
| `discovery_server` | 服务端，运行在发现中心容器（仅 discovery_server 后端需要） |
| `discovery_client` | 客户端，部署在每个业务容器，支持双后端 |

## 启动方式

### Discovery Server（discovery_server 后端）

```bash
./bin/discovery_server --server_port=8100
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8100 | 监听端口 |
| `--heartbeat_check_interval_ms` | 1000 | 健康检查扫描间隔（ms） |
| `--heartbeat_grace_factor` | 2.0 | 心跳超时倍数，超过 interval×factor 标记 DOWN |
| `--cleanup_factor` | 5.0 | 清理倍数，超过 interval×factor 从注册表移除 |

### Discovery Client

在每个业务服务容器中额外启动 `discovery_client`：

**discovery_server 后端：**

```bash
./discovery_client \
  --service_type=<service_name> \
  --service_port=<service_port> \
  --discovery_addr=<discovery_server_ip>:8100
```

**etcd 后端：**

```bash
./discovery_client \
  --service_type=<service_name> \
  --service_port=<service_port> \
  --registry_backend=etcd \
  --etcd_endpoints=<etcd1>:2379,<etcd2>:2379
```

**必填参数：**

| 参数 | 说明 |
|------|------|
| `--service_type` | 服务类型名，snake_case 格式（如 proxy, feature_service） |
| `--service_port` | 本容器主服务的监听端口 |

**后端选择参数：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--registry_backend` | "discovery_server" | 注册后端：`discovery_server` 或 `etcd` |
| `--discovery_addr` | "127.0.0.1:8100" | Discovery Server 地址（discovery_server 后端） |
| `--etcd_endpoints` | "127.0.0.1:2379" | etcd 节点地址，逗号分隔（etcd 后端） |

**可选参数：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | "auto" | 本容器 IP，auto 表示自动获取 |
| `--heartbeat_interval` | 5 | 心跳间隔（秒） |
| `--health_check_timeout` | 2 | TCP 端口探测超时（秒） |
| `--fail_threshold` | 3 | 连续失败次数阈值，超过则反注册 |
| `--startup_timeout` | 30 | 等待主服务端口就绪超时（秒） |

### 消费者服务（Proxy / RankMaster）

Proxy 和 RankMaster 也支持双后端，通过相同 flag 选择：

```bash
# discovery_server 后端（默认）
proxy_server --discovery_addr=discovery:8100

# etcd 后端
proxy_server --registry_backend=etcd --etcd_endpoints=etcd:2379
```

## 容器搭建

### Discovery Server 镜像

```bash
docker build -t linquickrec/discovery:latest \
  -f deploy/docker/discovery/Dockerfile .
```

```bash
docker run -d --name discovery-service \
  -p 8100:8100 \
  linquickrec/discovery:latest
```

### 业务容器中的 Discovery Client

在每个业务服务的 Dockerfile 中复制 `discovery_client` 二进制，并在 entrypoint 中启动：

```bash
# 使用 discovery_server 后端
/app/discovery_client \
  --service_type=$SERVICE_TYPE \
  --service_port=$SERVICE_PORT \
  --discovery_addr=$DISCOVERY_ADDR &

# 使用 etcd 后端
/app/discovery_client \
  --service_type=$SERVICE_TYPE \
  --service_port=$SERVICE_PORT \
  --registry_backend=etcd \
  --etcd_endpoints=$ETCD_ENDPOINTS &
```

## 端到端示例

`examples/` 目录包含完整的 docker-compose 演示，参见 [examples/README.md](examples/README.md)。

### 使用 etcd 后端的 docker-compose 示例

在 `docker-compose.yml` 中添加 etcd 服务并配置：

```yaml
services:
  etcd:
    image: quay.io/coreos/etcd:v3.5
    command:
      - etcd
      - --listen-client-urls=http://0.0.0.0:2379
      - --advertise-client-urls=http://etcd:2379

  recall_service:
    image: linquickrec/recall:latest
    environment:
      - SERVICE_TYPE=recall_service
      - SERVICE_PORT=8001
      - REGISTRY_BACKEND=etcd
      - ETCD_ENDPOINTS=etcd:2379
```
