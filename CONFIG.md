# LingQuickRec 配置参考

## 关于配置级别

| 配置级别 | 含义 |
|---------|------|
| **必须指定** | 无默认值或环境相关，部署前必须配置 |
| **允许调整** | 有默认值，可根据场景修改 |
| **不建议修改** | 有合理默认值，极少需要变更 |

## 全局配置

| 参数 | 默认值 | 配置级别 | 说明 |
|------|--------|---------|------|
| `DISCOVERY_ADDR` | discovery-server:8100 | 允许调整 | Discovery 服务地址（容器网络下使用容器名） |
| `DISCOVERY_PORT` | 8100 | 允许调整 | 服务发现中心端口 |
| `PROXY_PORT` | 8080 | 允许调整 | 网关对外 HTTP 端口 |

## Discovery

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8100 | 允许调整 | 监听端口 |
| `--heartbeat_check_interval_ms` | int32 | 1000 | 不建议修改 | 健康检查扫描间隔 (ms) |
| `--heartbeat_grace_factor` | double | 2.0 | 不建议修改 | 心跳超时倍数 |
| `--cleanup_factor` | double | 5.0 | 不建议修改 | 清理倍数 |

### Server 端参数（Discovery）

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_num_threads` | int32 | 0 | 允许调整 | bthread 工作线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 不建议修改 | 空闲连接超时(秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 允许调整 | 最大并发请求数，0=不限制 |

## Proxy

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8080 | 允许调整 | HTTP 监听端口 |
| `--discovery_addr` | string | — | 必须指定 | Discovery 地址（取自全局 `DISCOVERY_ADDR`） |
| `--discovery_refresh_interval_ms` | int32 | 5000 | 不建议修改 | Discovery 命名服务缓存刷新间隔 (ms) |
| `--feature_service_name` | string | "feature_service" | 允许调整 | Feature 服务在 Discovery 中的注册名 |
| `--recall_service_name` | string | "recall_service" | 允许调整 | Recall 服务在 Discovery 中的注册名 |
| `--precalc_service_name` | string | "precalc_service" | 允许调整 | Precalc 服务在 Discovery 中的注册名 |
| `--rank_service_name` | string | "rank_service" | 允许调整 | Rank 服务在 Discovery 中的注册名 |

### Server 端参数（Proxy）

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_num_threads` | int32 | 0 | 允许调整 | bthread 工作线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 不建议修改 | 空闲连接超时(秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 允许调整 | 最大并发请求数，0=不限制 |

### Client 端参数 — 下游通道共享（Proxy）

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--downstream_connection_type` | string | "pooled" | 允许调整 | 下游通道连接类型 (single/pooled/short) |
| `--downstream_max_retry` | int32 | 3 | 允许调整 | 下游通道 BRPC 重试次数 |
| `--downstream_connect_timeout_ms` | int32 | -1 | 允许调整 | 下游通道 TCP 建连超时(ms)，-1=禁用 |

### Client 端参数 — 下游通道按服务（Proxy）

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--feature_timeout_ms` | int32 | 3000 | 允许调整 | Feature 服务 RPC 超时(ms) |
| `--feature_backup_request_ms` | int32 | -1 | 允许调整 | Feature backup request 延迟阈值(ms)，-1=禁用 |
| `--recall_timeout_ms` | int32 | 5000 | 允许调整 | Recall 服务 RPC 超时(ms) |
| `--recall_backup_request_ms` | int32 | -1 | 允许调整 | Recall backup request 延迟阈值(ms)，-1=禁用 |
| `--precalc_timeout_ms` | int32 | 5000 | 允许调整 | Precalc 服务 RPC 超时(ms) |
| `--precalc_backup_request_ms` | int32 | -1 | 允许调整 | Precalc backup request 延迟阈值(ms)，-1=禁用 |
| `--rank_timeout_ms` | int32 | 10000 | 允许调整 | Rank 服务 RPC 超时(ms) |
| `--rank_backup_request_ms` | int32 | -1 | 允许调整 | Rank backup request 延迟阈值(ms)，-1=禁用 |

### Client 端参数 — Discovery 命名服务通道（Proxy）

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--discovery_naming_timeout_ms` | int32 | 3000 | 允许调整 | 命名服务调 Discovery 超时(ms) |
| `--discovery_naming_connection_type` | string | "single" | 不建议修改 | 命名服务到 Discovery 的连接类型 |
| `--discovery_naming_max_retry` | int32 | 2 | 不建议修改 | 命名服务通道重试次数 |
| `--discovery_naming_connect_timeout_ms` | int32 | -1 | 允许调整 | 命名服务通道 TCP 建连超时(ms)，-1=禁用 |
| `--discovery_naming_backup_request_ms` | int32 | -1 | 允许调整 | 命名服务通道 backup request(ms)，-1=禁用 |

## Discovery Client（sidecar）

所有服务容器通过 discovery_client 向 discovery server 注册。以下参数由 entrypoint.sh 注入。`--discovery_addr` 取自全局配置 `DISCOVERY_ADDR`，其余参数均有合理默认值，通常无需修改。

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--service_type` | string | — | 必须指定 | 服务类型名 |
| `--service_port` | int32 | — | 必须指定 | 本容器主服务端口 |
| `--discovery_addr` | string | (全局 `DISCOVERY_ADDR`) | 必须指定 | Discovery 地址 |
| `--host` | string | "auto" | 不建议修改 | 本容器 IP |
| `--heartbeat_interval` | int32 | 5 | 不建议修改 | 心跳间隔 (秒) |
| `--health_check_timeout` | int32 | 2 | 不建议修改 | TCP 探测超时 (秒) |
| `--fail_threshold` | int32 | 3 | 不建议修改 | 连续失败次数阈值 |
| `--startup_timeout` | int32 | 30 | 不建议修改 | 等待主服务就绪超时 (秒) |

### Client 端参数（Discovery Client → Discovery Server）

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--discovery_client_timeout_ms` | int32 | 5000 | 允许调整 | Discovery Client RPC 超时(ms) |
| `--discovery_client_connection_type` | string | "single" | 不建议修改 | 到 Discovery 的连接类型 |
| `--discovery_client_max_retry` | int32 | 2 | 不建议修改 | 通道重试次数 |
| `--discovery_client_connect_timeout_ms` | int32 | -1 | 允许调整 | TCP 建连超时(ms)，-1=禁用 |
| `--discovery_client_backup_request_ms` | int32 | -1 | 允许调整 | Backup request 延迟阈值(ms)，-1=禁用 |

## Feature

### 业务参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8001 | 允许调整 | 监听端口 |
| `--user_log_count` | int32 | 10 | 允许调整 | 每次响应的用户日志数量 |
| `--user_log_vec_size` | int32 | 30 | 允许调整 | 每条用户日志向量大小 |
| `--sku_feat_length` | int32 | 20 | 允许调整 | SKU 特征字符串长度 |

### Server 端参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_num_threads` | int32 | 0 | 允许调整 | bthread 工作线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 不建议修改 | 空闲连接超时(秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 允许调整 | 最大并发请求数，0=不限制 |

## Recall

### 业务参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8002 | 允许调整 | 监听端口 |
| `--vllm_base_url` | string | "http://127.0.0.1:8000" | 允许调整 | vLLM 服务基础 URL |
| `--vllm_endpoint` | string | "/v1/chat/completions" | 允许调整 | vLLM 聊天接口端点 |
| `--model_name` | string | "/workspace/share/Qwen3-0.6B/" | 允许调整 | 模型名称/路径 |
| `--sku_count` | int32 | 100 | 允许调整 | 返回的 SKU ID 数量 |

### Server 端参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_num_threads` | int32 | 0 | 允许调整 | bthread 工作线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 不建议修改 | 空闲连接超时(秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 允许调整 | 最大并发请求数，0=不限制 |

### Client 端参数（Recall → vLLM）

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--vllm_timeout_ms` | int32 | 100000 | 允许调整 | vLLM RPC 超时(ms) |
| `--vllm_connection_type` | string | "single" | 允许调整 | 到 vLLM 的连接类型 (single/pooled/short) |
| `--vllm_max_retry` | int32 | 3 | 允许调整 | 到 vLLM 的重试次数 |
| `--vllm_connect_timeout_ms` | int32 | -1 | 允许调整 | TCP 建连超时(ms)，-1=禁用 |
| `--vllm_backup_request_ms` | int32 | -1 | 允许调整 | Backup request 延迟阈值(ms)，-1=禁用 |

## Precalc

### 业务参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8003 | 允许调整 | 监听端口 |
| `--kvworker_host` | string | "141.61.84.245" | 必须指定 | 元戎 KVWorker 主机地址 |
| `--kvworker_port` | int32 | 31502 | 必须指定 | 元戎 KVWorker 端口 |
| `--etcd_address` | string | "141.61.84.245:2379" | 必须指定 | etcd 集群地址（SDK 内定义） |
| `--precalc_result_size_mb` | double | 8.5 | 允许调整 | 前置计算结果大小 (MB) |
| `--ttl_seconds` | int32 | 5 | 允许调整 | KV 缓存 TTL 时间（秒） |
| `--payload_size_kb` | int32 | 100 | 允许调整 | Payload 大小 (KB) |

### Server 端参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_num_threads` | int32 | 0 | 允许调整 | bthread 工作线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 不建议修改 | 空闲连接超时(秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 允许调整 | 最大并发请求数，0=不限制 |

## RankMaster

### 业务参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8004 | 允许调整 | 监听端口 |
| `--discovery_addr` | string | "" | 必须指定 | Discovery 地址（取自全局 `DISCOVERY_ADDR`） |
| `--top_k` | int32 | 100 | 允许调整 | 返回前 K 个商品 |
| `--sub_worker_service_type` | string | "rank_sub" | 允许调整 | RankSub 在 Discovery 中的注册服务类型名 |
| `--sub_worker_parallelism` | int32 | 4 | 允许调整 | 并发分桶数 |

### Server 端参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_num_threads` | int32 | 0 | 允许调整 | bthread 工作线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 不建议修改 | 空闲连接超时(秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 允许调整 | 最大并发请求数，0=不限制 |

### Client 端参数 — RankMaster → RankSub

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--sub_worker_timeout_ms` | int32 | 5000 | 允许调整 | RankSub 调用超时(ms) |
| `--sub_worker_connection_type` | string | "pooled" | 允许调整 | 到 RankSub 的连接类型 (single/pooled/short) |
| `--sub_worker_max_retry` | int32 | 3 | 允许调整 | 到 RankSub 的重试次数 |
| `--sub_worker_connect_timeout_ms` | int32 | -1 | 允许调整 | TCP 建连超时(ms)，-1=禁用 |
| `--sub_worker_backup_request_ms` | int32 | -1 | 允许调整 | Backup request 延迟阈值(ms)，-1=禁用 |

## RankSub

### 业务参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8005 | 允许调整 | 监听端口 |
| `--kvworker_host` | string | "141.61.84.245" | 必须指定 | KVWorker 主机地址 |
| `--kvworker_port` | int32 | 31502 | 必须指定 | KVWorker 端口 |
| `--etcd_address` | string | "141.61.84.245:2379" | 必须指定 | etcd 集群地址（SDK 内定义） |
| `--scoring_delay_ms` | int32 | 100 | 允许调整 | 模拟打分耗时 (ms) |

### Server 端参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_num_threads` | int32 | 0 | 允许调整 | bthread 工作线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 不建议修改 | 空闲连接超时(秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 允许调整 | 最大并发请求数，0=不限制 |

## KV Worker (Datasystem)

KV Worker 通过 `docker run` 独立启动（不在 docker-compose.yml 中），以下参数通过环境变量传入。启动脚本 `services/kv_worker/start_datasystem.sh` 内部调用 `dscli start --worker_args` 启动 Datasystem Worker。

### 环境变量

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `worker_address` | string | -- | 必须指定 | Worker 监听地址 (IP:port)，不能使用 127.0.0.1 |
| `etcd_address` | string | -- | 必须指定 | Etcd 集群地址 (IP:port) |
| `enable_urma` | int | -- | 必须指定 | 是否启用 URMA (0/1) |
| `cpu_affinity` | string | "0-64" | 允许调整 | CPU 核绑定范围 |

### dscli 启动参数

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--shared_memory_size_mb` | int | 2048 | 不建议修改 | 共享内存大小 (MB) |
| `--arena_per_tenant` | int | 1 | 不建议修改 | 每 tenant 的 arena 数 |
| `--skip_authenticate` | int | 1 | 不建议修改 | 跳过认证 |
| `--urma_mode` | string | "UB" | 不建议修改 | URMA 模式 |
| `--minloglevel` | int | 1 | 不建议修改 | 最小日志级别 |
| `--oc_thread_num` | int | 64 | 不建议修改 | OC 线程数 |
| `--oc_shm_transfer_threshold_kb` | int | 0 | 不建议修改 | 共享内存传输阈值 (KB) |
| `--zmq_server_io_context` | int | 16 | 不建议修改 | ZMQ 服务端 IO 上下文数 |
| `--zmq_client_io_context` | int | 16 | 不建议修改 | ZMQ 客户端 IO 上下文数 |
