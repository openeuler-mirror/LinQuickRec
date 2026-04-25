# 服务发现中心设计方案

## 1. 设计目标

在容器化部署环境下，每服务实例的 ip 和 port 在启动时未知。需要一套服务发现机制，使服务之间能动态感知彼此的可用地址。

**核心需求**：
- 服务发现中心（`discovery_server`）以容器形式独立运行，维护所有注册服务的实例列表及健康状态
- 每个服务容器内运行一个独立的 `discovery_client` 进程，负责向中心注册和发送心跳
- 对原服务代码**零侵入性**：不修改现有服务的 main.cpp、CMakeLists.txt 或代码逻辑
- 容器内健康检查基于 TCP 端口探测，反映真实的服务可用性
- 服务可以从服务发现中心获取下游服务的可用列表，自行实现负载均衡策略

## 2. 方案对比与选型依据

| 维度 | 独立 Client 进程（本方案） | In-Process Library |
|------|--------------------------|-------------------|
| 代码侵入 | **零** — 不改一行现有代码 | 每个服务改 main.cpp + CMakeLists.txt |
| 语言无关 | 任意语言编写的服务均可接入 | 仅限 C++ |
| 健康感知 | TCP 端口探测（外部视角，真实可靠） | 可获取内部状态（更精细但耦合） |
| 进程管理 | 多一个进程 — 需处理信号转发、启动顺序 | 天然单进程 |
| 构建分发 | 编译一个静态二进制，COPY 到各容器 | 修改每个服务的 CMake + 重新编译 |

本方案选择独立 Client 进程，以实现 **零侵入性** 和 **语言无关性** 为首要目标。

## 3. 系统架构

```
┌──────────────────────────────────────────────────────────────────────┐
│                       Discovery Server (:8100)                        │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │                       In-Memory Registry                        │  │
│  │  "feature_service" → [{instance_id, host, port, status, ts}]   │  │
│  │  "recall_service"  → [{...}, {...}]                              │  │
│  │  "precalc_service" → [{...}]                                     │  │
│  │  "rank_master"     → [{...}]                                     │  │
│  │  "rank_sub"        → [{...}, {...}, ...]                         │  │
│  │  "proxy"           → [{...}]                                     │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌──────────────────┐  │
│  │Register() │  │Deregister │  │Heartbeat()│  │   Discover()     │  │
│  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘  └────────┬─────────┘  │
│        │              │              │                  │             │
│  ┌─────▼──────────────▼──────────────▼──────────────────▼──────┐     │
│  │            Health Check Timer (background, 1s interval)      │     │
│  │    last_heartbeat > 2 x interval  -> mark DOWN               │     │
│  │    last_heartbeat > 5 x interval  -> auto remove             │     │
│  └──────────────────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────────────────┘
            ▲                                    │
            │ Register / Heartbeat                │ Discover
            │ Deregister                          ▼
   ┌─────────────────┐                 ┌───────────────────────┐
   │   Service A     │                 │   Consumer (Proxy)    │
   │   container     │                 │       container       │
   │                 │                 │                        │
   │ ┌─────────────┐ │                 │ 1. Discover()          │
   │ │ main service│ │                 │ 2. pick instance      │
   │ │   :8003     │ │                 │ 3. create BRPC Channel│
   │ └─────────────┘ │                 │ 4. call RPC           │
   │                 │                 └───────────────────────┘
   │ ┌─────────────┐ │
   │ │ discovery   │ │<--- TCP probe :8003 health status
   │ │   _client   │ │---> periodic Register / Heartbeat / Deregister
   │ └─────────────┘ │
   └─────────────────┘
```

## 4. 目录结构

```
services/discovery/
├── DESIGN.md                    # 本文档
├── README.md                    # 模块介绍与使用说明
├── CMakeLists.txt               # 顶层 CMake（add_subdirectory server + client）
├── Dockerfile                   # 编译 server + client
├── proto/
│   └── discovery.proto          # Protobuf 服务定义
├── server/
│   ├── include/
│   │   └── discovery_server.h   # DiscoveryServiceImpl 声明
│   └── src/
│       ├── main.cpp             # Discovery Server 入口
│       └── discovery_server.cpp # 服务实现：注册/心跳/发现 + 健康检查线程
└── client/
    ├── CMakeLists.txt           # 独立可执行文件
    └── src/
        └── main.cpp             # discovery_client 入口：注册 + 端口探测 + 心跳 + 信号处理
```

## 5. Protobuf 协议定义

**文件**：`proto/discovery.proto`

协议定义了四个 RPC：

- **Register**：实例注册自身（服务名、host、port），服务端分配全局唯一 `instance_id` 并返回确认的心跳间隔
- **Deregister**：实例主动注销，从注册表移除
- **Heartbeat**：实例周期发送，服务端更新 `last_heartbeat` 时间戳
- **Discover**：消费者查询某服务所有 `UP` 状态的实例列表

消息结构：
- `ServiceInstance` 包含服务名、host、port、instance_id、元数据 map、状态（UP/DOWN）、最后心跳时间
- `RegisterRequest` 包含实例信息和建议心跳间隔
- `RegisterResponse` 返回 success、message、分配的 instance_id、服务端确认的心跳间隔
- `DeregisterRequest` 包含服务名和 instance_id
- `HeartbeatResponse` 包含 `needs_reregister` 标志，当服务端丢失该实例状态时通知客户端重新注册
- `DiscoverResponse` 仅返回 `status=UP` 的实例

## 6. 组件详述

### 6.1 Discovery Server

负责维护服务注册表，提供四个 RPC 接口，并在后台运行健康检查线程。

**RPC 接口**：

| 接口 | 功能 |
|------|------|
| `Register` | 注册一个新实例，分配 `instance_id`，返回确认的心跳间隔 |
| `Deregister` | 注销一个实例，将其从注册表移除 |
| `Heartbeat` | 更新实例的 `last_heartbeat` 时间戳 |
| `Discover` | 查询某服务的所有 UP 状态实例 |

**健康检查线程（后台）**：
- 每 `heartbeat_check_interval_ms`（默认 1s）扫描所有实例
- `now - last_heartbeat > interval × grace_factor（2.0）` → 标记 `DOWN`
- `now - last_heartbeat > interval × cleanup_factor（5.0）` → 从注册表移除
- 标记/移除操作记录日志

### 6.2 Discovery Client

独立的可执行文件，每个服务容器中运行一个实例。**对原服务代码零侵入**。

**启动流程**：

1. 解析命令行参数（参数列表见第 8 节）
2. 等待主服务端口就绪（最多 `startup_timeout` 秒），超时则日志警告但仍尝试注册
3. 向 Discovery Server 发起 `Register()`，失败时指数退避重试
4. 进入主循环（每 `heartbeat_interval` 秒）：
   - TCP 端口探测判断主服务是否健康
   - 健康 → fail_count 归零，发送 `Heartbeat()`
   - 不健康 → fail_count 递增，连续达到 `fail_threshold` 次则发送 `Deregister()`
   - 健康但已反注册 → 重新 `Register()`
5. 捕获 `SIGTERM/SIGINT` 信号时，先发送 `Deregister()` 再退出
6. 异常退出（如被 kill -9）由 Server 侧心跳超时兜底标记 DOWN

**健康检查方式**：
- 使用 TCP Socket 连接 `127.0.0.1:service_port`
- 连接超时由 `health_check_timeout` 参数控制
- 连接成功则认为服务健康，连接失败则认为不健康

**关键设计要点**：
- **启动等待**：client 先等主服务端口就绪再注册，避免注册了一个不可用的服务
- **抖动防护**：连续 `fail_threshold` 次端口探测失败才 Deregister，防止瞬时抖动导致频繁上下线
- **自动重注册**：服务恢复后自动重新注册，无需人工干预
- **信号安全**：捕获 SIGTERM/SIGINT 确保宕前反注册
- **Server 兜底**：即使 client 被 kill -9 或容器被 `docker kill`，Server 侧的心跳超时检测仍会将其标记 DOWN，保证注册表最终一致性

### 6.3 消费者使用 Discovery 的方式

消费者（如 Proxy）自行实现负载均衡，Discovery Server 不介入选择逻辑。

**标准流程**：
1. 周期性调用 `Discover(service_name)` 获取最新实例列表
2. 本地缓存该列表
3. 每次需要调用下游时：从列表中选择一个实例（RoundRobin / Random / 自定义策略），创建 BRPC Channel 后发起 RPC
4. 可选：设置定时器定期刷新缓存

**静态 fallback**：
若 Discovery Server 不可用（网络问题或 Server 宕机），消费者可保留最后一次 `Discover()` 缓存的实例列表继续工作。也可通过独立参数配置一组静态地址作为 fallback。发现中心恢复后自动切回动态发现。

## 7. 容器集成方案

### 7.1 Discovery Server 容器

Dockerfile 基于 `lingquickrec/base:latest`，编译 `discovery_server` 和 `discovery_client` 两个二进制。容器启动命令为 `./build/discovery_server`，暴露端口 8100。

### 7.2 各服务容器适配方法

每个现有服务的 Dockerfile 只需新增一步 COPY，将 `discovery_client` 二进制从 discovery 构建产物中复制到本镜像中。

启动方式使用 Docker 的 `--init` 标志（等价于注入 tini 作为 PID 1），确保容器停止时 `SIGTERM` 能被正确转发给两个子进程。

典型命令模式：
```
/app/gateway_server --server_port=8080 &
/app/discovery_client --service_type=proxy --service_port=8080 --discovery_addr=discovery:8100 &
wait
```

两个进程在后台启动后 `wait` 等待任意一个退出，容器退出时 `--init` 保证信号转发。

## 8. 配置参数总表

### 8.1 Discovery Server

| 参数名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `--server_port` | int32 | 8100 | 监听端口 |
| `--heartbeat_check_interval_ms` | int32 | 1000 | 健康检查扫描间隔（毫秒） |
| `--heartbeat_grace_factor` | float | 2.0 | 心跳超时倍数 |
| `--cleanup_factor` | float | 5.0 | 清理倍数 |

### 8.2 Discovery Client

| 参数名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `--service_type` | string | (必填) | 服务类型，snake_case 格式，如 proxy、feature_service、recall_service |
| `--service_port` | int32 | (必填) | 本容器主服务监听端口 |
| `--discovery_addr` | string | "127.0.0.1:8100" | Discovery Server 地址 |
| `--host` | string | "auto" | 本容器 IP，auto 表示自动获取 |
| `--heartbeat_interval` | int32 | 5 | 心跳间隔（秒） |
| `--health_check_timeout` | int32 | 2 | TCP 端口探测超时（秒） |
| `--fail_threshold` | int32 | 3 | 连续失败次数阈值 |
| `--startup_timeout` | int32 | 30 | 等待主服务端口就绪超时（秒） |

## 9. 端口分配

| 服务 | 端口 | 说明 |
|------|------|------|
| Discovery Service | 8100 | 服务发现中心监听端口 |

## 10. 状态流转

```
                Register() success
    ┌─────────────────────────────────────┐
    │                                     ▼
    │   ┌──────────┐   heartbeat ok    ┌─────┐
    │   │ UNKNOWN  │──────────────────▶│ UP  │
    │   └──────────┘                   └──┬──┘
    │         ▲                           │
    │         │    heartbeat timeout      │
    │         │    (2 x interval)         │
    │         │                           ▼
    │         │                       ┌───────┐
    │         └───────────────────────│ DOWN  │
    │                                 └───┬───┘
    │                                     │
    │                               timeout cleanup
    │                               (5 x interval)
    │                                     ▼
    │                                 (REMOVED)
    │
    └────────── Deregister() ─────────▶ (REMOVED)
```

## 11. 错误处理与边界情况

| 场景 | 行为 |
|------|------|
| **Discovery Server 宕机** | Client 心跳重试失败 → 持续重试；Consumer 使用本地缓存继续服务 |
| **Discovery Server 重启** | Client 心跳失败 → 收到 `needs_reregister=true` → 自动重新注册 |
| **主服务启动慢** | Client 等待端口就绪最多 `startup_timeout` 秒，超时后仍注册（降级模式） |
| **主服务瞬间重启** | Client 检测到端口短暂不可用 → `fail_count++`，恢复后继续心跳 |
| **主服务永久崩溃** | Client 连续 `fail_threshold` 次失败 → `Deregister()` → Server 标记已下线 |
| **Client 被 kill -9** | Server 心跳超时检测兜底 → 自动 DOWN |
| **容器被 docker kill（无 SIGTERM）** | Server 心跳超时检测兜底 |
| **网络分区** | Server 侧心跳超时 → DOWN；分区恢复后 Client 心跳恢复 → UP |
| **双/多实例启动同名服务** | 每个实例获得独立 `instance_id`，Server 正常维护所有实例 |

## 12. 演进规划

| 版本 | 特性 |
|------|------|
| **V1.0** | 核心功能：独立 client 进程 + 端口探测 + 注册/心跳/发现/健康检查 |
| **V1.1** | Consumer 侧支持 Watch/Notify 推送（长轮询），降低发现延迟 |
| **V1.2** | 元数据路由：Client 注册时上报版本号、标签，Consumer 按条件过滤 |
| **V2.0** | Discovery Server 多副本高可用（Raft 共识） |
