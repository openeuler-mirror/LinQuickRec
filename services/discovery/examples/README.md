# 服务发现示例

演示场景：部署一个 `discovery-server` 容器 + 多个伪服务容器，伪服务自动注册到发现中心，并通过 `test_discover` 查询验证。

## 目录结构

```
services/discovery/examples/
├── README.md                 # 本文件（全流程操作指导）
├── CMakeLists.txt            # 编译 pseudo_service + test_discover
├── Dockerfile                # 伪服务容器镜像（多阶段构建）
├── docker-compose.yml        # 一键编排所有容器
├── entrypoint.sh             # 容器入口：启动 pseudo_service + discovery_client
├── pseudo_service/
│   └── main.cpp              # 简易 TCP server，模拟业务服务
└── test_discover/
    └── main.cpp              # 调用 Discover RPC 查询实例列表
```

## 前置条件

- Docker 及 docker compose
- 项目根目录存在 `proto/discovery.proto`

## 编译

### 1. 编译 discovery_server

```bash
cd <project-root>
mkdir -p build && cd build
cmake ..
make discovery_server -j$(nproc)
```

### 2. 编译 pseudo_service 和 test_discover

```bash
cd services/discovery/examples
mkdir -p build && cd build
cmake ..
make pseudo_service test_discover -j$(nproc)
```

编译产物：

| 二进制 | 路径 | 用途 |
|--------|------|------|
| `build/pseudo_service` | 伪服务 | 模拟业务服务，监听 TCP 端口 |
| `build/test_discover` | 查询工具 | 向发现中心发起 Discover RPC |

## 容器化搭建

### 1. 构建镜像并启动全部容器

```bash
# 在项目根目录执行
docker compose -f services/discovery/examples/docker-compose.yml build --no-cache
docker compose -f services/discovery/examples/docker-compose.yml up -d
```

启动 4 个容器：

| 容器名 | 服务类型 | 监听端口 | 角色 |
|--------|---------|---------|------|
| discovery-server | — | 8100 | 服务发现中心 |
| pseudo-recall | recall_service | 8001 | 伪召回服务 |
| pseudo-feature | feature_service | 8002 | 伪特征服务 |
| pseudo-proxy | proxy | 8003 | 伪网关 |

### 2. 查看容器日志确认注册成功

```bash
docker compose -f services/discovery/examples/docker-compose.yml logs pseudo-recall
```

预期输出（每 5s 一条心跳日志）：

```
discovery_client: Registered as recall_service_172.17.0.3_8001_1
discovery_client: Heartbeat OK
```

## 验证测试

使用 `test_discover` 在任意容器内查询各服务类型的实例列表：

```bash
# 查询 recall_service
docker compose -f services/discovery/examples/docker-compose.yml exec pseudo-recall \
  test_discover --server=discovery-server:8100 recall_service

# 查询 feature_service
docker compose -f services/discovery/examples/docker-compose.yml exec pseudo-feature \
  test_discover --server=discovery-server:8100 feature_service

# 查询 proxy
docker compose -f services/discovery/examples/docker-compose.yml exec pseudo-proxy \
  test_discover --server=discovery-server:8100 proxy

# 查询未部署的服务类型（应返回 0 个实例）
docker compose -f services/discovery/examples/docker-compose.yml exec pseudo-recall \
  test_discover --server=discovery-server:8100 rank_master
```

### 预期输出

```
# test_discover --server=discovery-server:8100 recall_service
Found 1 instance(s) of [recall_service]:
  [0] recall_service_172.17.0.3_8001_1  172.17.0.3:8001  status=UP

# test_discover --server=discovery-server:8100 feature_service
Found 1 instance(s) of [feature_service]:
  [0] feature_service_172.17.0.4_8002_1  172.17.0.4:8002  status=UP

# test_discover --server=discovery-server:8100 proxy
Found 1 instance(s) of [proxy]:
  [0] proxy_172.17.0.5_8003_1  172.17.0.5:8003  status=UP

# test_discover --server=discovery-server:8100 rank_master
Found 0 instance(s) of [rank_master]:
```

### 验证要点

| 查询 service_type | 应返回实例数 | 说明 |
|------------------|-------------|------|
| recall_service | 1 | 正常注册的伪召回服务 |
| feature_service | 1 | 正常注册的伪特征服务 |
| proxy | 1 | 正常注册的伪网关 |
| rank_master | 0 | 未部署该服务 |

每个实例的 `host:port` 应与对应容器的 IP 和 `SERVICE_PORT` 一致，`status` 应为 UP。

## 清理

```bash
docker compose -f services/discovery/examples/docker-compose.yml down
```
