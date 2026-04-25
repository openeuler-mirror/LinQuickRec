# 服务发现中心 — Service Discovery

## 模块简介

服务发现中心提供了一套独立于业务代码的服务注册与发现机制，解决容器化部署场景下服务实例地址动态变化的难题。

本模块包含两个可执行文件：

- **discovery_server** — 服务发现中心服务端，运行在独立容器中，维护注册表并执行健康检查
- **discovery_client** — 独立的辅助进程，部署在每个业务服务容器中，负责向服务端注册本实例并周期发送心跳

模块采用零侵入式设计，业务服务无需修改任何代码即可接入服务发现。

## 目录结构

```
services/discovery/
├── DESIGN.md               # 详细设计文档
├── README.md               # 本文件
├── CMakeLists.txt          # CMake 配置（支持单独构建与父工程子目录两种模式）
├── Dockerfile              # Docker 构建文件
├── server/
│   ├── include/
│   │   └── discovery_server.h
│   └── src/
│       ├── main.cpp
│       └── discovery_server.cpp
└── client/
    └── src/
        └── main.cpp
```

## 编译

支持两种构建方式：

### 方式一：在 discovery 目录内单独构建

```bash
cd services/discovery
mkdir -p build && cd build
cmake ..
make discovery_server discovery_client -j$(nproc)
```

### 方式二：在项目根目录整体构建

```bash
mkdir -p build && cd build
cmake ..
make discovery_server discovery_client -j$(nproc)
```

### 编译产物

| 二进制 | 路径 | 用途 |
|--------|------|------|
| `build/discovery_server` | 服务端 | 运行在发现中心容器 |
| `build/discovery_client` | 客户端 | 每个业务容器内运行一份 |

## 使用方法

### 启动 Discovery Server

```bash
./discovery_server --server_port=8100
```

可选参数：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8100 | 监听端口 |
| `--heartbeat_check_interval_ms` | 1000 | 健康检查扫描间隔（ms） |
| `--heartbeat_grace_factor` | 2.0 | 心跳超时倍数，last_heartbeat 超过 interval×factor 标记 DOWN |
| `--cleanup_factor` | 5.0 | 清理倍数，超过 interval×factor 从注册表移除 |

### 启动 Discovery Client

在每个业务服务的容器中额外启动 `discovery_client`：

```bash
./discovery_client \
  --service_type=recall_service \
  --service_port=8001 \
  --discovery_addr=discovery:8100
```

必填参数：

| 参数 | 说明 |
|------|------|
| `--service_type` | 服务类型名，使用 snake_case 格式（如 proxy、feature_service、recall_service、precalc_service、rank_master、rank_sub） |
| `--service_port` | 本容器主服务的监听端口 |

可选参数：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--discovery_addr` | "127.0.0.1:8100" | Discovery Server 地址 |
| `--host` | "auto" | 本容器 IP，auto 表示自动获取 |
| `--heartbeat_interval` | 5 | 心跳间隔（秒） |
| `--health_check_timeout` | 2 | TCP 端口探测超时（秒） |
| `--fail_threshold` | 3 | 连续失败次数阈值，超过则反注册 |
| `--startup_timeout` | 30 | 等待主服务端口就绪超时（秒） |

### Docker 集成

**Discovery Server** Dockerfile：

```dockerfile
FROM lingquickrec/base:latest
WORKDIR /app
COPY services/discovery/CMakeLists.txt /app/
COPY services/discovery/server /app/server/
COPY services/discovery/client /app/client/
COPY proto/discovery.proto /app/../../proto/
RUN mkdir -p build && cd build && cmake .. && make -j$(nproc)
EXPOSE 8100
CMD ["./build/discovery_server"]
```

**业务服务容器**：在现有 Dockerfile 中添加：

```dockerfile
COPY --from=discovery-build /app/build/discovery_client /app/discovery_client
```

启动时使用 `--init` 标志并同时拉起两个进程：

```bash
docker run --init --name recall-service \
  recall-image \
  sh -c "/app/recall_server --server_port=8001 & \
         /app/discovery_client --service_type=recall_service --service_port=8001 --discovery_addr=discovery:8100 & \
         wait"
```

`--init` 注入 tini 作为 PID 1，确保容器停止时 `SIGTERM` 正确转发给两个子进程。

### 消费者端使用

需要调用下游服务的消费者（如 Proxy）通过 BRPC 调用 Discovery Server 的 `Discover` RPC 获取实例列表，自行实现选择逻辑：

1. 周期性调用 `Discover("feature_service")` 获取 UP 实例列表
2. 本地缓存实例列表
3. 每次调用下游时从列表中选择一个实例（RoundRobin / Random 等），创建 BRPC Channel 发起调用

## 工作流程

```
       service container
  ┌──────────────────────────┐
  │   main service (:8003)   │
  └──────────┬───────────────┘
             │  TCP probe
  ┌──────────▼───────────────┐
  │    discovery_client      │───> Register → Discovery Server (:8100)
  │                          │───> heartbeat every 5s
  │                          │───> port down → Deregister
  │                          │───> SIGTERM   → Deregister
  └──────────────────────────┘
```

## 服务类型名对照表

| snake_case | 对应服务 |
|------------|---------|
| `proxy` | Proxy 网关 |
| `feature_service` | FeatureService |
| `recall_service` | RecallService |
| `precalc_service` | PrecalcService |
| `rank_master` | RankServiceMaster |
| `rank_sub` | RankServiceSub |
