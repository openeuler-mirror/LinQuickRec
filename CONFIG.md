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
| `DISCOVERY_PORT` | 8100 | 允许调整 | 服务发现中心端口 |
| `PROXY_PORT` | 8080 | 允许调整 | 网关对外 HTTP 端口 |
| `KVWORKER_HOST` | — | 必须指定 | 元戎数据系统主机地址 |
| `KVWORKER_PORT` | — | 必须指定 | 元戎数据系统端口 |
| `ETCD_ADDRESS` | — | 必须指定 | ETCD 地址 |

## Discovery

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8100 | 允许调整 | 监听端口 |
| `--heartbeat_check_interval_ms` | int32 | 1000 | 不建议修改 | 健康检查扫描间隔 (ms) |
| `--heartbeat_grace_factor` | double | 2.0 | 不建议修改 | 心跳超时倍数 |
| `--cleanup_factor` | double | 5.0 | 不建议修改 | 清理倍数 |

## Proxy

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8080 | 允许调整 | HTTP 监听端口 |
| `--discovery_addr` | string | "127.0.0.1:8100" | 允许调整 | Discovery 地址 |
| `--discovery_refresh_interval_ms` | int32 | 5000 | 不建议修改 | 缓存刷新间隔 (ms) |
| `--downstream_max_retries` | int32 | 2 | 不建议修改 | 下游最大重试次数 |
| `--feature_service_name` | string | "feature_service" | 不建议修改 | Feature 服务注册名 |
| `--recall_service_name` | string | "recall_service" | 不建议修改 | Recall 服务注册名 |
| `--precalc_service_name` | string | "precalc_service" | 不建议修改 | Precalc 服务注册名 |
| `--rank_service_name` | string | "rank_service" | 不建议修改 | RankMaster 服务注册名 |
| `--feature_timeout_ms` | int32 | 3000 | 允许调整 | Feature 调用超时 (ms) |
| `--recall_timeout_ms` | int32 | 5000 | 允许调整 | Recall 调用超时 (ms) |
| `--precalc_timeout_ms` | int32 | 5000 | 允许调整 | Precalc 调用超时 (ms) |
| `--rank_timeout_ms` | int32 | 10000 | 允许调整 | Rank 调用超时 (ms) |
| `--enable_timing_stats` | bool | true | 不建议修改 | 打印阶段时延统计 |
| `--global_thread_pool_size` | int32 | auto | 允许调整 | 全局线程池大小 |

## Recall

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8002 | 允许调整 | 监听端口 |
| `--vllm_base_url` | string | "http://127.0.0.1:8000" | 允许调整 | vLLM 地址 |
| `--vllm_endpoint` | string | "/v1/chat/completions" | 允许调整 | vLLM 接口路径 |
| `--model_name` | string | "/app/models/Qwen3-0.6B/" | 允许调整 | 模型路径（容器内） |
| `--vllm_timeout_ms` | int32 | 100000 | 允许调整 | vLLM 请求超时 (ms) |
| `--sku_count` | int32 | 100 | 允许调整 | 返回 SKU 数量 |
| `VLLM_PORT` | 8000 | 允许调整 | vLLM 端口 |
| `VLLM_STARTUP_TIMEOUT` | 120 | 不建议修改 | vLLM 启动等待秒数 |

## Precalc

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8003 | 允许调整 | 监听端口 |
| `--kvworker_host` | string | — | 必须指定 | KVWorker 地址 |
| `--kvworker_port` | int32 | — | 必须指定 | KVWorker 端口 |
| `--etcd_address` | string | — | 必须指定 | ETCD 地址 |
| `--precalc_result_size_mb` | double | 8.5 | 允许调整 | 预计算结果大小 (MB) |
| `--ttl_seconds` | int32 | 5 | 允许调整 | KV 缓存 TTL |
| `--user_feat_key_size_kb` | int32 | 100 | 不建议修改 | user_feat_key 大小 (KB) |
| `--enable_timing_stats` | bool | true | 不建议修改 | 启用时延统计 |
| `--payload_size_kb` | int32 | 100 | 允许调整 | payload 大小 (KB) |

## RankMaster

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8004 | 允许调整 | 监听端口 |
| `--sub_worker_count` | int32 | 3 | 允许调整 | 子图数量 |
| `--sub_worker_addresses` | string | "rank-sub-service:8005" | 允许调整 | 子图地址 |
| `--top_k` | int32 | 100 | 允许调整 | 返回 Top-K |
| `--enable_timing_stats` | bool | true | 不建议修改 | 启用时延统计 |
| `SUB_WORKER_TIMEOUT_MS` | 5000 | 不建议修改 | 子图请求超时 (ms) |
| `RANK_SUB_STARTUP_TIMEOUT` | 120 | 不建议修改 | 等待 RankSub 就绪秒数 |

## RankSub

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8005 | 允许调整 | 监听端口 |
| `--kvworker_host` | string | — | 必须指定 | KVWorker 地址 |
| `--kvworker_port` | int32 | — | 必须指定 | KVWorker 端口 |
| `--etcd_address` | string | — | 必须指定 | ETCD 地址 |
| `--scoring_delay_ms` | int32 | 100 | 允许调整 | 打分延迟 (ms) |
| `--enable_timing_stats` | bool | true | 不建议修改 | 启用时延统计 |

## 扩缩容参数

| 参数 | 默认值 | 配置级别 | 说明 |
|------|--------|---------|------|
| `RANK_SUB_REPLICAS` | 3 | 允许调整 | RankSub 副本数 |
| `SUB_WORKER_COUNT` | 3 | 允许调整 | 需与 `RANK_SUB_REPLICAS` 一致 |

## Discovery Client（sidecar）

所有服务容器通过 discovery_client 向 discovery server 注册。以下参数由 entrypoint.sh 注入。

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--service_type` | string | — | 必须指定 | 服务类型名 |
| `--service_port` | int32 | — | 必须指定 | 本容器主服务端口 |
| `--discovery_addr` | string | "127.0.0.1:8100" | 允许调整 | Discovery 地址 |
| `--host` | string | "auto" | 不建议修改 | 本容器 IP |
| `--heartbeat_interval` | int32 | 5 | 允许调整 | 心跳间隔 (秒) |
| `--health_check_timeout` | int32 | 2 | 不建议修改 | TCP 探测超时 (秒) |
| `--fail_threshold` | int32 | 3 | 不建议修改 | 连续失败次数阈值 |
| `--startup_timeout` | int32 | 30 | 允许调整 | 等待主服务就绪超时 (秒) |
