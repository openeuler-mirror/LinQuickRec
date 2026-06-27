# kv_worker 服务

## 模块简介

kv_worker 是元戎 Datasystem 分布式 KV 缓存的 worker 节点。它通过 `dscli start --worker_args` 启动 datasystem worker 进程，同时运行 [discovery 服务](../discovery/README.md) 的 sidecar client 将自身注册到服务发现，供 precalc / rank-sub 等服务动态发现。

kv_worker 与普通 BRPC 微服务不同：客户端（precalc 的 `KVClient.Set`、rank-sub 的 `KVClient.Get`）与 worker 之间的对象数据传输走**宿主机共享内存**（POSIX shm, `/dev/shm`），控制面/元数据走网络。因此要求客户端 Pod 与 worker 在同一物理节点上，且共享同一块 `/dev/shm`。

### 可观测性

worker 日志写入 `./datasystem_log/log_<worker_port>/`，通过 `--minloglevel` 控制日志级别。discovery_client sidecar 向 stdout 输出注册/心跳日志。

## 目录结构

```
services/kv_worker/
├── README.md
├── run_kv_worker_container.sh        (旧版，仅供参考)

deploy/docker/kv_worker/
├── Dockerfile                        容器镜像定义
├── entrypoint.sh                     容器入口脚本
└── start_datasystem.sh               dscli worker 启动脚本
```

### 阶段时序

| 阶段 | 调用方式 | 依赖 | 说明 |
|------|---------|------|------|
| DNS 解析 | 同步 | /etc/hosts 或 DNS server | 将 `etcd_address` 和 `ETCD_ENDPOINTS` 中的主机名解析为 IP，元戎 SDK 不接受主机名 |
| 启动 datasystem worker | 异步（后台） | etcd 可访问 | `dscli start` 启动后注册到 etcd，等待客户端连接 |
| 启动 discovery sidecar | 同步（前台阻塞） | discovery server / etcd | 周期性心跳注册 kv_worker 到服务发现，供 precalc / rank-sub 发现 |

## 编译命令

kv_worker 没有 C++ 源码需要编译，`dscli` 二进制由基础镜像 `linquickrec/base:latest` 内置。容器构建时仅编译 `discovery_client`（来自 `services/discovery/`）作为 sidecar。

### 构建镜像

```bash
# 在项目根目录下执行
docker build -t linquickrec/kv_worker:latest \
  -f deploy/docker/kv_worker/Dockerfile .
```

Dockerfile 构建流程：

1. 基于 `linquickrec/base:latest`
2. 复制 `proto/`、`common/`、`services/discovery/` 源码
3. 编译 `discovery_client` 二进制 → `/app/discovery_client`
4. 复制 `start_datasystem.sh` → `/workerspace/start_datasystem.sh`
5. 复制 `entrypoint.sh` → `/entrypoint.sh`，设为 ENTRYPOINT

## 启动方式

### datasystem worker 参数

以下参数通过环境变量传入，由 `start_datasystem.sh` 转发给 `dscli start --worker_args`：

| 参数 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| `--worker_address` | `worker_address` | **必填** | worker 注册到 etcd 的可达地址，格式 `IP:Port`，不能写 `127.0.0.1` |
| `--etcd_address` | `etcd_address` | **必填** | etcd 服务器地址，格式 `IP:Port` |
| `--enable_urma` | `enable_urma` | **必填** | 是否启用 URMA，`0`=不启用，`1`=启用（x86 推荐 0） |
| `--host_id_env_name` | (固定) | `HOST_ID` | 节点亲和匹配所依据的环境变量名，需与客户端一致 |
| `--shared_memory_size_mb` | `shared_memory_size_mb` | `2048` | worker 共享内存大小 (MB) |
| `--cpu_affinity` | `cpu_affinity` | `0-64` | CPU 核心绑定范围，通过 `taskset -c` 设置 |
| `--log_dir` | (固定) | `./datasystem_log/log_<worker_port>` | 日志输出目录 |
| `--arena_per_tenant` | (固定) | `1` | 每租户 arena 数 |
| `--skip_authenticate` | (固定) | `1` | 跳过认证 |
| `--urma_mode` | (固定) | `UB` | URMA 模式 |
| `--minloglevel` | (固定) | `1` | 最小日志级别 (0=INFO, 1=WARNING, 2=ERROR) |
| `--oc_thread_num` | (固定) | `64` | OC 线程数 |
| `--oc_shm_transfer_threshold_kb` | (固定) | `0` | 对象传输共享内存阈值 (KB)，0=所有对象走 shm |
| `--zmq_server_io_context` | (固定) | `16` | ZMQ 服务端 IO 上下文数 |
| `--zmq_client_io_context` | (固定) | `16` | ZMQ 客户端 IO 上下文数 |

### discovery sidecar 参数

由 `entrypoint.sh` 解析环境变量后转发给 `/app/discovery_client`：

| 参数 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| `--service_type` | (固定) | `kv_worker` | 注册到 discovery 的服务类型 |
| `--service_port` | (自动) | `worker_address` 的端口部分 | worker 监听端口 |
| `--registry_backend` | `REGISTRY_BACKEND` | `discovery_server` | 注册后端类型，`etcd` 或 `discovery_server` |
| `--etcd_endpoints` | `ETCD_ENDPOINTS` | `etcd:2379` | etcd 端点 (仅 `registry_backend=etcd` 时生效) |
| `--discovery_addr` | `DISCOVERY_ADDR` | `discovery-server:8100` | discovery server 地址 (仅 `registry_backend=discovery_server` 时生效) |
| `--host` | `HOST` | `auto` | worker 主机标识，`auto` 表示自动检测 |
| `--heartbeat_interval` | `HEARTBEAT_INTERVAL` | `5` | 心跳间隔 (秒) |
| `--health_check_timeout` | `HEALTH_CHECK_TIMEOUT` | `2` | 健康检查超时 (秒) |
| `--fail_threshold` | `FAIL_THRESHOLD` | `3` | 连续失败阈值 |
| `--startup_timeout` | `STARTUP_TIMEOUT` | `30` | 启动超时 (秒) |

### 直接启动 (dscli)

```bash
# 非容器环境下直接启动 worker（通常不推荐，建议用容器）
taskset -c 0-64 \
dscli start --worker_args \
    --worker_address "X.X.X.X:31501" \
    --etcd_address "X.X.X.X:2379" \
    --host_id_env_name HOST_ID \
    --shared_memory_size_mb 2048 \
    --log_dir "./datasystem_log/log_31501" \
    --arena_per_tenant 1 \
    --skip_authenticate 1 \
    --enable_urma 0 \
    --urma_mode UB \
    --minloglevel 1 \
    --oc_thread_num 64 \
    --oc_shm_transfer_threshold_kb 0 \
    --zmq_server_io_context 16 \
    --zmq_client_io_context 16
```

## 容器搭建

### 构建镜像

```bash
# 在项目根目录下执行
docker build -t linquickrec/kv_worker:latest \
  -f deploy/docker/kv_worker/Dockerfile .
```

### 运行容器

```bash
# 启动前需在宿主机设置好 huge page
docker run -d \
  --name kv_worker \
  --restart=always \
  --privileged \
  --ipc=host \
  --net=host \
  -v /dev/shm:/dev/shm \
  -e worker_address="X.X.X.X:31501" \
  -e etcd_address="X.X.X.X:2379" \
  -e enable_urma=0 \
  linquickrec/kv_worker:latest
```

关键 Docker 参数说明：

| 参数 | 必要性 | 说明 |
|------|--------|------|
| `--privileged` | **必须** | datasystem 需要访问宿主机 shm、CPU 亲和 (taskset) 等特权操作 |
| `--ipc=host` | **必须** | 共享主机 IPC 命名空间，客户端才能 attach worker 的共享内存段 |
| `--net=host` | **必须** | worker 注册的地址必须在客户端可达网络上 |
| `-v /dev/shm:/dev/shm` | **必须** | 挂载宿主机共享内存，与同节点客户端共用 |
| `-e worker_address` | **必须** | worker 注册地址，**必须是宿主机 IP**，不能写 `127.0.0.1` |
| `-e etcd_address` | **必须** | etcd 可访问地址 |
| `-e enable_urma` | **必须** | `0` 或 `1` |

**注意：**
- 宿主机需预先设置 huge page（`echo 4096 > /proc/sys/vm/nr_hugepages`）
- 如需启动多个 worker 实例（不同端口），修改 `--name` 和 `worker_address` 端口即可，确保各实例 `worker_address` 端口不冲突
- K8s 环境建议使用 DaemonSet 部署，使每个节点运行一个 worker，匹配客户端 `PREFERRED_SAME_NODE` 亲和策略。详见 [datasystem skill](../../.agents/skills/datasystem/SKILL.md)

## 测试方法

### 手动测试

```bash
# 在容器内检查 worker 进程
docker exec kv_worker ps aux | grep dscli

# 在容器内检查 discovery 注册状态
docker exec kv_worker cat /workspace/*.log 2>/dev/null

# 检查 datasystem worker 日志
docker exec kv_worker ls ./datasystem_log/
docker exec kv_worker tail -f ./datasystem_log/log_31501/worker.log
```

### 验证客户端连通性

从 precalc 或 rank-sub 容器中检查是否能发现并连接 kv_worker：

```bash
# 查看 precalc 日志中的 KV 操作是否成功
docker logs precalc 2>&1 | grep -E "KVClient (Set|Get)"

# 成功标志
# KVClient Set success
# KVClient Get success (key=..., value_size=...)
```
