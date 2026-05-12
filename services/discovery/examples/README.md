# 服务发现示例

## 模块简介

本示例提供了一套 docker-compose 编排，演示 discovery 系统的完整工作流程：启动一个 `discovery-server` 实例，同时部署多个伪业务服务（pseudo_service）作为不同服务类型的多副本实例，通过测试工具验证服务注册、发现、心跳及生命周期管理等核心功能。

架构要点：
- 伪服务（pseudo_service）为轻量 TCP server，无业务逻辑，仅用于模拟服务注册与健康检查
- 每个伪服务容器内同时运行 `pseudo_service` + `discovery_client`，形成完整的注册/心跳链路
- 测试工具通过 BRPC 协议与 `discovery-server` 交互，无需外部依赖
- 零侵入设计：业务服务无需修改代码即可通过 discovery_client 接入服务发现

### 可观测性

- `pseudo_service` 使用 `common::logger` 输出端口监听状态与健康检查日志
- `discovery_client` 日志展示注册结果、心跳状态与反注册事件
- `discovery-server` 日志记录完整的注册/心跳/DOWN/清理事件链

## 目录结构

```
services/discovery/examples/
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

下面展示一次完整 demo 部署的调用链路与状态流转：

```
                    docker compose up
                          │
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Stage 1: discovery-server starts                                   │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  listening on port 8100 for RPC requests                     │   │
│  │  (Register / Heartbeat / Deregister / Discover)              │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                          │
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Stage 2: pseudo-service containers start and register              │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  pseudo_service starts TCP listener on SERVICE_PORT          │   │
│  │  discovery_client sends Register to discovery-server         │   │
│  │  discovery_client sends heartbeat every 5s                   │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                          │
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Stage 3: Run verification tests via test-client container          │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  test_discover     -> query instances by service_type        │   │
│  │  test_register     -> register + deregister + confirm removal │  │
│  │  test_heartbeat_cycle -> UP -> DOWN -> REMOVED lifecycle     │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 阶段时序

| 阶段 | 操作 | 参与组件 | 说明 |
|---|---|---|---|
| Stage 1 | 启动 discovery-server | discovery-server | 监听 8100 端口，等待 RPC |
| Stage 2 | 伪服务注册 | pseudo_service, discovery_client | 每个容器独立注册，同类型多副本共享 service_type |
| Stage 2 (持续) | 心跳维持 | discovery_client -> discovery-server | 每 5s 发送心跳，超时阈值通过 factor 控制 |
| Stage 3 | 实例查询 | test_discover -> discovery-server | 按 service_type 查询 UP 实例列表 |
| Stage 3 | 注册/反注册验证 | test_register -> discovery-server | 验证 Register + Deregister RPC 正确性 |
| Stage 3 | 生命周期验证 | test_heartbeat_cycle -> discovery-server | 验证 UP -> DOWN -> REMOVED 完整链路 |

### 错误处理

| 场景 | 表现 |
|---|---|
| discovery-server 未启动时注册 | discovery_client 重试直到连接成功或超时 |
| 心跳丢失超过阈值 | discovery-server 将实例标记为 DOWN |
| DOWN 状态持续超过清理时间 | discovery-server 从注册表移除该实例 |
| 伪服务端口未就绪 | discovery_client 等待端口探测成功（--startup_timeout） |
| 连续健康检查失败 | discovery_client 主动反注册（--fail_threshold） |

### trace_id

`discovery_client` 在每次 Register / Heartbeat / Deregister RPC 调用中生成全局唯一 `trace_id`（格式为 UUID 字符串），通过 BRPC 的 Attachment 机制传递至 `discovery-server`，用于全链路日志关联。

## 编译命令

### 依赖

| 依赖 | 版本要求 | 备注 |
|------|----------|------|
| CMake | >= 3.14 | 编译工具链 |
| brpc | >= 1.4 | `linquickrec/base:latest` 基础镜像已内置 |
| protobuf | >= 3.0 | `linquickrec/base:latest` 基础镜像已内置 |
| abseil-cpp | latest | `linquickrec/base:latest` 基础镜像已内置 |

### 脚本构建

```bash
cd services/discovery/examples
./build.sh              # 默认为 Release 构建
./build.sh release      # Release 构建
./build.sh debug        # Debug 构建
./build.sh clean        # 清理后构建
```

### 手动构建

```bash
cd services/discovery/examples
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 编译产物

| 二进制 | 说明 |
|--------|------|
| `pseudo_service` | 模拟业务服务，监听 TCP 端口 |
| `test_discover` | 查询指定 service_type 的实例列表 |
| `test_register` | 验证 Register + Deregister RPC |
| `test_heartbeat_cycle` | 验证全生命周期健康检查 |

## 启动方式

### 容器环境变量

| 参数 | 默认值 | 说明 |
|---|---|---|
| `SERVICE_TYPE` | (必填) | 服务类型名，snake_case 格式 |
| `SERVICE_PORT` | (必填) | 本容器主服务的监听端口 |
| `DISCOVERY_ADDR` | "discovery-server:8100" | Discovery Server 地址 |

### 直接启动（本地调试）

```bash
# 启动 pseudo_service
./build/bin/pseudo_service --port=8002

# 启动 discovery_client（需先编译）
./build/bin/discovery_client \
    --service_type=my_service \
    --service_port=8002 \
    --discovery_addr=127.0.0.1:8100
```

## 容器搭建

### 镜像构建

```bash
docker compose -f services/discovery/examples/docker-compose.yml build
```

### 启动全部容器

```bash
docker compose -f services/discovery/examples/docker-compose.yml up -d
```

### 容器一览

启动 9 个容器：

| 容器名 | 镜像名 | 服务类型 | 端口 | 副本数 |
|---|---|---|---|---|
| discovery-examples-server | discovery-examples-server | — | 8100 | 1 |
| discovery-examples-proxy | discovery-examples-pseudo | proxy | 8002 | 1 |
| discovery-examples-feature | discovery-examples-pseudo | feature_service | 8002 | 1 |
| discovery-examples-recall-{1,2,3} | discovery-examples-pseudo | recall_service | 8001 | 3 |
| discovery-examples-rank-{1,2,3} | discovery-examples-pseudo | rank_service | 8003 | 3 |
| discovery-examples-client | discovery-examples-pseudo | — | — | 1 |

各伪服务容器自动运行 `pseudo_service + discovery_client`，向发现中心注册。同类型容器使用相同端口（各自容器内独立，互不冲突）。

### 查看容器日志确认注册成功

**Discovery Server：**

```bash
docker compose -f services/discovery/examples/docker-compose.yml logs discovery-server
```

预期输出：

```
[2026-04-29 11:14:15.123] [INFO] [main.cpp:42] Discovery Server starting on 8100
[2026-04-29 11:14:15.124] [INFO] [main.cpp:43] Heartbeat check interval: 1000ms
[2026-04-29 11:14:16.962] [INFO] [discovery_server.cpp:74] Register: proxy_172.19.0.3_8002_1
[2026-04-29 11:14:17.237] [INFO] [discovery_server.cpp:74] Register: feature_service_172.19.0.8_8002_1
[2026-04-29 11:14:17.421] [INFO] [discovery_server.cpp:74] Register: recall_service_172.19.0.4_8001_1
[2026-04-29 11:14:17.592] [INFO] [discovery_server.cpp:74] Register: rank_service_172.19.0.9_8003_1
...
```

**任一伪服务容器（如 pseudo-recall-1）：**

```bash
docker compose -f services/discovery/examples/docker-compose.yml logs pseudo-recall-1
```

预期输出：

```
========================================
Pseudo service starting
  service_type: recall_service
  service_port: 8001
  discovery_addr: discovery-server:8100
========================================
[2026-04-29 11:14:17.410] [INFO] [main.cpp:48] Pseudo service listening on port 8001
[2026-04-29 11:14:17.412] [INFO] [main.cpp:140] Discovery Client starting
[2026-04-29 11:14:17.412] [INFO] [main.cpp:163] Main service port 8001 is ready
[2026-04-29 11:14:17.412] [INFO] [main.cpp:68] Port 8001 accepting health checks
[2026-04-29 11:14:17.412] [INFO] [main.cpp:69] (subsequent health check logs are suppressed)
[2026-04-29 11:14:17.415] [INFO] [main.cpp:202] Registered as recall_service_172.19.0.4_8001_1
[2026-04-29 11:14:22.418] [INFO] [main.cpp:218] Heartbeat OK              <- 每 5s 一条
[2026-04-29 11:14:27.422] [INFO] [main.cpp:218] Heartbeat OK
...
```

### 清除资源

```bash
# 停止并移除所有容器
docker compose -f services/discovery/examples/docker-compose.yml down

# 删除构建的镜像
docker rmi discovery-examples-server discovery-examples-pseudo
```

## 测试方法

### 手动验证测试

在宿主机上，通过 `docker compose exec` 在容器内执行测试工具。建议使用 `test-client` 容器（无业务进程干扰）。

#### 测试 1：查询各服务类型实例

```bash
# proxy
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_discover --server=discovery-server:8100 proxy

# feature_service
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_discover --server=discovery-server:8100 feature_service

# recall_service
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_discover --server=discovery-server:8100 recall_service

# rank_service
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_discover --server=discovery-server:8100 rank_service

# 未部署类型
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_discover --server=discovery-server:8100 no_this_service
```

预期输出：

```
[PASS] Found 1 instance(s) of [proxy]:
  [0] proxy_172.17.0.x_8002_1  172.17.0.x:8002  status=UP

[PASS] Found 1 instance(s) of [feature_service]:
  [0] feature_service_172.17.0.x_8002_1  172.17.0.x:8002  status=UP

[PASS] Found 3 instance(s) of [recall_service]:
  [0] recall_service_172.17.0.x_8001_1  172.17.0.x:8001  status=UP
  [1] recall_service_172.17.0.x_8001_1  172.17.0.x:8001  status=UP
  [2] recall_service_172.17.0.x_8001_1  172.17.0.x:8001  status=UP

[PASS] Found 3 instance(s) of [rank_service]:
  [0] rank_service_172.17.0.x_8003_1  172.17.0.x:8003  status=UP
  [1] rank_service_172.17.0.x_8003_1  172.17.0.x:8003  status=UP
  [2] rank_service_172.17.0.x_8003_1  172.17.0.x:8003  status=UP

[PASS] Found 0 instance(s) of [no_this_service]:
```

#### 测试 2：注册与反注册

```bash
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_register \
  --server=discovery-server:8100 \
  --service_type=_test_ \
  --host=127.0.0.1 \
  --port=10000
```

预期输出：

```
[PASS] Registered as _test__127.0.0.1_10000_1
[PASS] Deregistered _test__127.0.0.1_10000_1
[PASS] Confirmed 0 instances of [_test_]
[PASS] test_register passed
```

#### 测试 3：全生命周期健康检查

```bash
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_heartbeat_cycle \
    --server=discovery-server:8100 \
    --service_type=_test_ \
    --host=127.0.0.1 --port=10000 \
    --heartbeat_interval=3
```

预期输出（共需约 20s，含等待 DOWN + 清理的时间）：

```
[PASS] Step 1: Registered as _test__127.0.0.1_10000_1
[PASS] Step 2: Heartbeat accepted
[PASS] Step 3: Instance is UP
[INFO] Waiting for server to mark instance DOWN (~6s)...
[PASS] Step 4: Instance is DOWN
[INFO] Waiting for server to remove instance (~9s)...
[PASS] Step 5: Instance cleaned up
[PASS] test_heartbeat_cycle passed
```

### 测试要点对照

| 测试 | 验证点 | 预期结果 |
|---|---|---|
| `test_discover proxy` | 单实例查询 | 1 个 UP 实例 |
| `test_discover feature_service` | 单实例查询 | 1 个 UP 实例 |
| `test_discover recall_service` | 同类型多副本 | 3 个 UP 实例 |
| `test_discover rank_service` | 同类型多副本 | 3 个 UP 实例 |
| `test_discover rank_master` | 未部署服务 | 0 个实例 |
| `test_register` | Register + Deregister RPC | 注册成功 -> 反注册成功 -> 确认已删除 |
| `test_heartbeat_cycle` | 心跳保持 UP -> 停心跳变 DOWN -> 超时清理 | 5 步全部 PASS |
