# 服务发现示例

演示场景：部署一个 `discovery-server` + 多个伪服务实例（不同服务类型多副本），通过测试工具验证各项功能。

## 目录结构

```
services/discovery/examples/
├── README.md                 # 本文件（全流程操作指导）
├── CMakeLists.txt            # 编译 pseudo_service + 测试工具
├── Dockerfile                # 伪服务容器镜像
├── docker-compose.yml        # 一键编排所有容器
├── entrypoint.sh             # 容器入口：启动 pseudo_service + discovery_client
├── pseudo_service/
│   └── main.cpp              # 简易 TCP server，模拟业务服务
└── tests/
    ├── test_discover.cpp       # 查询 Discover RPC
    ├── test_register.cpp       # 测试 Register + Deregister RPC
    └── test_heartbeat_cycle.cpp# 全生命周期：注册 → 心跳 → DOWN → 清理
```

## 前置条件

- 编译机已安装 brpc、abseil、protobuf
- 编译机已安装 docker 及 docker compose

## 编译

### 1. 编译 discovery_server + discovery_client

参考 `services/discovery` 目录下的 `README.md` 文件完成编译。

**编译产物：**

- `discovery_server`
- `discovery_client`

### 2. 编译示例和测试工具

```bash
cd services/discovery/examples
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

| 编译产物 | 用途 |
|--------|------|
| `build/bin/pseudo_service` | 模拟业务服务，监听 TCP 端口 |
| `build/bin/test_discover` | 查询指定 service_type 的实例列表 |
| `build/bin/test_register` | 验证 Register + Deregister RPC |
| `build/bin/test_heartbeat_cycle` | 验证全生命周期健康检查 |

## 容器化搭建

### 1. 构建镜像并启动全部容器

```bash
# 在项目根目录执行
docker compose -f services/discovery/examples/docker-compose.yml build --no-cache
docker compose -f services/discovery/examples/docker-compose.yml up -d
```

> **覆盖二进制路径**：若编译产物位置与默认不同，可通过 `--build-arg` 指定：
>
> ```bash
> # 例如从 services/discovery/ 目录单独编译后，在 project root 执行：
> docker compose -f services/discovery/examples/docker-compose.yml build \
>   --build-arg DISCOVERY_SERVER_BIN=services/discovery/build/bin/discovery_server \
>   --build-arg DISCOVERY_CLIENT_BIN=services/discovery/build/bin/discovery_client
> ```
>
> 完整参数列表见 [examples/Dockerfile](Dockerfile) 中的 `ARG` 定义。

### 2. 容器一览

启动 9 个容器：

| 容器名 | 镜像名 | 服务类型 | 容器内端口 | 副本数 |
|--------|--------|---------|-----------|--------|
| discovery-examples-server | discovery-examples-server | — | 8100 | 1 |
| discovery-examples-proxy | discovery-examples-pseudo | proxy | 8001 | 1 |
| discovery-examples-feature | discovery-examples-pseudo | feature_service | 8002 | 1 |
| discovery-examples-recall-{1,2,3} | discovery-examples-pseudo | recall_service | 8003 | 3 |
| discovery-examples-rank-{1,2,3} | discovery-examples-pseudo | rank_service | 8004 | 3 |
| discovery-examples-client | discovery-examples-pseudo | — | — | 1 |

各伪服务容器自动运行 `pseudo_service + discovery_client`，向发现中心注册。同类型容器使用相同端口（各自容器内独立，互不冲突）。

### 3. 查看容器日志确认注册成功

```bash
docker compose -f services/discovery/examples/docker-compose.yml logs pseudo-recall-1
```

预期输出（每 5s 一条心跳日志）：

```
discovery_client: Registered as recall_service_172.17.0.3_8003_1
discovery_client: Heartbeat OK
```

### 4. 清除资源

```bash
# 停止并移除所有容器
docker compose -f services/discovery/examples/docker-compose.yml down

# 删除构建的镜像
docker rmi discovery-examples-server discovery-examples-pseudo
```

## 手动验证测试

在宿主机上，通过 `docker compose exec` 在容器内执行测试工具。建议使用 `test-client` 容器（无业务进程干扰）。

### 测试 1：查询各服务类型实例

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

**预期输出：**

```
[PASS] Found 1 instance(s) of [proxy]:
  [0] proxy_172.17.0.x_8001_1  172.17.0.x:8001  status=UP

[PASS] Found 1 instance(s) of [feature_service]:
  [0] feature_service_172.17.0.x_8002_1  172.17.0.x:8002  status=UP

[PASS] Found 3 instance(s) of [recall_service]:
  [0] recall_service_172.17.0.x_8003_1  172.17.0.x:8003  status=UP
  [1] recall_service_172.17.0.x_8003_1  172.17.0.x:8003  status=UP
  [2] recall_service_172.17.0.x_8003_1  172.17.0.x:8003  status=UP

[PASS] Found 3 instance(s) of [rank_service]:
  [0] rank_service_172.17.0.x_8004_1  172.17.0.x:8004  status=UP
  [1] rank_service_172.17.0.x_8004_1  172.17.0.x:8004  status=UP
  [2] rank_service_172.17.0.x_8004_1  172.17.0.x:8004  status=UP

[PASS] Found 0 instance(s) of [no_this_service]:
```

### 测试 2：注册与反注册

```bash
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_register \
  --server=discovery-server:8100 \
  --service_type=_test_ \
  --host=127.0.0.1 \
  --port=10000
```

**预期输出：**

```
[PASS] Registered as _test__127.0.0.1_10000_1
[PASS] Deregistered _test__127.0.0.1_10000_1
[PASS] Confirmed 0 instances of [_test_]
[PASS] test_register passed
```

### 测试 3：全生命周期健康检查

```bash
docker compose -f services/discovery/examples/docker-compose.yml exec test-client \
  test_heartbeat_cycle \
    --server=discovery-server:8100 \
    --service_type=_test_ \
    --host=127.0.0.1 --port=10000 \
    --heartbeat_interval=3
```

**预期输出（共需约 20s，含等待 DOWN + 清理的时间）：**

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

## 测试要点对照

| 测试 | 验证点 | 预期结果 |
|------|--------|---------|
| `test_discover proxy` | 单实例查询 | 1 个 UP 实例 |
| `test_discover feature_service` | 单实例查询 | 1 个 UP 实例 |
| `test_discover recall_service` | 同类型多副本 | 3 个 UP 实例 |
| `test_discover rank_service` | 同类型多副本 | 3 个 UP 实例 |
| `test_discover rank_master` | 未部署服务 | 0 个实例 |
| `test_register` | Register + Deregister RPC | 注册成功 → 反注册成功 → 确认已删除 |
| `test_heartbeat_cycle` | 心跳保持 UP → 停心跳变 DOWN → 超时清理 | 5 步全部 PASS |


