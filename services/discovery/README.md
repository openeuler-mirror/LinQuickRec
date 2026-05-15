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
├── build.sh
├── CMakeLists.txt
├── DESIGN.md
├── README.md
├── client/
│   └── src/
│       └── main.cpp
├── server/
│   ├── include/
│   │   └── discovery_server.h
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

### 服务注册与发现流程

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

### 核心交互

| 操作 | 方向 | 协议 | 说明 |
|------|------|------|------|
| Register | client → server | BRPC | 注册本实例（service_type, host, port） |
| Heartbeat | client → server | BRPC | 周期性心跳，重置超时计时器 |
| Deregister | client → server | BRPC | 优雅退出时主动反注册 |
| Discover | server → client | BRPC | 查询指定 service_type 的 UP 实例列表 |

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
| `discovery_server` | 服务端，运行在发现中心容器 |
| `discovery_client` | 客户端，部署在每个业务容器 |

## 启动方式

### Discovery Server

```bash
./bin/discovery_server --server_port=8100
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8100 | 监听端口 |
| `--heartbeat_check_interval_ms` | 1000 | 健康检查扫描间隔（ms） |
| `--heartbeat_grace_factor` | 2.0 | 心跳超时倍数，超过 interval×factor 标记 DOWN |
| `--cleanup_factor` | 5.0 | 清理倍数，超过 interval×factor 从注册表移除 |
| `--server_num_threads` | int32 | 0 | 服务端 bthread 线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 空闲连接超时 (秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 最大并发请求数，0=不限制 |

### Discovery Client

在每个业务服务容器中额外启动 `discovery_client`：

```bash
./discovery_client \
  --service_type=<service_name> \
  --service_port=<service_port> \
  --discovery_addr=<discovery_server_ip>:8100
```

**必填参数：**

| 参数 | 说明 |
|------|------|
| `--service_type` | 服务类型名，snake_case 格式（如 proxy, feature_service） |
| `--service_port` | 本容器主服务的监听端口 |

**可选参数：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--discovery_addr` | "127.0.0.1:8100" | Discovery Server 地址 |
| `--host` | "auto" | 本容器 IP，auto 表示自动获取 |
| `--heartbeat_interval` | 5 | 心跳间隔（秒） |
| `--health_check_timeout` | 2 | TCP 端口探测超时（秒） |
| `--fail_threshold` | 3 | 连续失败次数阈值，超过则反注册 |
| `--startup_timeout` | 30 | 等待主服务端口就绪超时（秒） |
| `--discovery_client_timeout_ms` | int32 | 5000 | Discovery 客户端超时 (ms) |
| `--discovery_client_connection_type` | string | "single" | Discovery 客户端连接类型 |
| `--discovery_client_max_retry` | int32 | 2 | Discovery 客户端 BRPC 重试次数 |
| `--discovery_client_connect_timeout_ms` | int32 | -1 | Discovery 客户端建连超时 (ms)，-1=禁用 |
| `--discovery_client_backup_request_ms` | int32 | -1 | Discovery 客户端 backup request (ms)，-1=禁用 |

## 容器搭建

### 镜像构建

```bash
docker build -t linquickrec/discovery:latest \
  -f deploy/docker/discovery/Dockerfile .
```

### 启动服务

```bash
docker run -d --name discovery-service \
  -p 8100:8100 \
  linquickrec/discovery:latest
```

## 端到端示例

`examples/` 目录包含完整的 docker-compose 演示，参见 [examples/README.md](examples/README.md)。
