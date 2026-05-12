# LingQuickRec 配置参考

## 关于生效方式

| 标注 | 含义 |
|------|------|
| 🔄 重启容器 | 修改 `.env` 后执行 `docker compose up -d` 生效 |
| 🔧 重新构建 | 修改后需 `docker compose build` 再启动 |
| 🚨 环境相关 | 部署时必须确认，无合理默认值 |

## 全局配置

| 参数 | 默认值 | 生效方式 | 说明 |
|------|--------|---------|------|
| `DISCOVERY_PORT` | 8100 | 🔄 重启容器 | 服务发现中心端口 |
| `PROXY_PORT` | 8080 | 🔄 重启容器 | 网关对外 HTTP 端口 |
| `KVWORKER_HOST` | 141.61.84.245 | 🚨 环境相关 | 元戎数据系统主机地址 |
| `KVWORKER_PORT` | 31502 | 🚨 环境相关 | 元戎数据系统端口 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | 🚨 环境相关 | ETCD 地址 |

## Discovery

| 参数 | 类型 | 默认值 | 生效方式 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8100 | 🔄 重启容器 | 监听端口 |
| `--heartbeat_check_interval_ms` | int32 | 1000 | 🔄 重启容器 | 健康检查扫描间隔 (ms) |
| `--heartbeat_grace_factor` | double | 2.0 | 🔄 重启容器 | 心跳超时倍数 |
| `--cleanup_factor` | double | 5.0 | 🔄 重启容器 | 清理倍数 |

## Proxy

| 参数 | 类型 | 默认值 | 生效方式 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8080 | 🔄 重启容器 | HTTP 监听端口 |
| `--discovery_addr` | string | "127.0.0.1:8100" | 🔄 重启容器 | Discovery 地址 |
| `--discovery_refresh_interval_ms` | int32 | 5000 | 🔄 重启容器 | 缓存刷新间隔 (ms) |
| `--downstream_max_retries` | int32 | 2 | 🔄 重启容器 | 下游最大重试次数 |
| `--feature_service_name` | string | "feature_service" | 🔄 重启容器 | Feature 服务注册名 |
| `--recall_service_name` | string | "recall_service" | 🔄 重启容器 | Recall 服务注册名 |
| `--precalc_service_name` | string | "precalc_service" | 🔄 重启容器 | Precalc 服务注册名 |
| `--rank_service_name` | string | "rank_service" | 🔄 重启容器 | RankMaster 服务注册名 |
| `--feature_timeout_ms` | int32 | 3000 | 🔄 重启容器 | Feature 调用超时 (ms) |
| `--recall_timeout_ms` | int32 | 5000 | 🔄 重启容器 | Recall 调用超时 (ms) |
| `--precalc_timeout_ms` | int32 | 5000 | 🔄 重启容器 | Precalc 调用超时 (ms) |
| `--rank_timeout_ms` | int32 | 10000 | 🔄 重启容器 | Rank 调用超时 (ms) |
| `--enable_timing_stats` | bool | true | 🔄 重启容器 | 打印阶段时延统计 |
| `--global_thread_pool_size` | int32 | auto | 🔄 重启容器 | 全局线程池大小 |

## Recall

| 参数 | 类型 | 默认值 | 生效方式 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8002 | 🔄 重启容器 | 监听端口 |
| `--vllm_base_url` | string | "http://127.0.0.1:8000" | 🔄 重启容器 | vLLM 地址 |
| `--vllm_endpoint` | string | "/v1/chat/completions" | 🔄 重启容器 | vLLM 接口路径 |
| `--model_name` | string | "/app/models/Qwen3-0.6B/" | 🔧 重新构建 | 模型路径 |
| `--vllm_timeout_ms` | int32 | 100000 | 🔄 重启容器 | vLLM 请求超时 (ms) |
| `--sku_count` | int32 | 100 | 🔄 重启容器 | 返回 SKU 数量 |
| `VLLM_PORT` | 8000 | 🔄 重启容器 | vLLM 健康检查端口 |
| `VLLM_STARTUP_TIMEOUT` | 120 | 🔄 重启容器 | vLLM 启动等待秒数 |

## Precalc

| 参数 | 类型 | 默认值 | 生效方式 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8003 | 🔄 重启容器 | 监听端口 |
| `--kvworker_host` | string | "141.61.84.245" | 🚨 环境相关 | KVWorker 地址 |
| `--kvworker_port` | int32 | 31502 | 🚨 环境相关 | KVWorker 端口 |
| `--etcd_address` | string | "141.61.84.245:2379" | 🚨 环境相关 | ETCD 地址 |
| `--precalc_result_size_mb` | double | 8.5 | 🔄 重启容器 | 预计算结果大小 (MB) |
| `--ttl_seconds` | int32 | 5 | 🔄 重启容器 | KV 缓存 TTL |
| `--user_feat_key_size_kb` | int32 | 100 | 🔄 重启容器 | user_feat_key 大小 (KB) |
| `--enable_timing_stats` | bool | true | 🔄 重启容器 | 启用时延统计 |
| `--payload_size_kb` | int32 | 100 | 🔄 重启容器 | payload 大小 (KB) |

## RankMaster

| 参数 | 类型 | 默认值 | 生效方式 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8004 | 🔄 重启容器 | 监听端口 |
| `--sub_worker_count` | int32 | 3 | 🔄 重启容器 | 子图数量 |
| `--sub_worker_addresses` | string | "rank-sub-service:8005" | 🔄 重启容器 | 子图地址 |
| `--top_k` | int32 | 100 | 🔄 重启容器 | 返回 Top-K |
| `--enable_timing_stats` | bool | true | 🔄 重启容器 | 启用时延统计 |
| `SUB_WORKER_TIMEOUT_MS` | 5000 | 🔄 重启容器 | 子图请求超时 (ms) |
| `RANK_SUB_STARTUP_TIMEOUT` | 120 | 🔄 重启容器 | 等待 RankSub 就绪秒数 |

## RankSub

| 参数 | 类型 | 默认值 | 生效方式 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8005 | 🔄 重启容器 | 监听端口 |
| `--kvworker_host` | string | "141.61.84.245" | 🚨 环境相关 | KVWorker 地址 |
| `--kvworker_port` | int32 | 31502 | 🚨 环境相关 | KVWorker 端口 |
| `--etcd_address` | string | "141.61.84.245:2379" | 🚨 环境相关 | ETCD 地址 |
| `--scoring_delay_ms` | int32 | 100 | 🔄 重启容器 | 打分延迟 (ms) |
| `--enable_timing_stats` | bool | true | 🔄 重启容器 | 启用时延统计 |

## 扩缩容参数

| 参数 | 默认值 | 生效方式 | 说明 |
|------|--------|---------|------|
| `RANK_SUB_REPLICAS` | 3 | 🔄 重启容器 | RankSub 副本数 |
| `SUB_WORKER_COUNT` | 3 | 🔄 重启容器 | 需与 `RANK_SUB_REPLICAS` 一致 |

## Discovery Client（sidecar）

所有服务容器通过 discovery_client 向 discovery server 注册。以下参数由 entrypoint.sh 注入。

| 参数 | 类型 | 默认值 | 生效方式 | 说明 |
|------|------|--------|---------|------|
| `--service_type` | string | — | 🚨 必填 | 服务类型名 |
| `--service_port` | int32 | — | 🚨 必填 | 本容器主服务端口 |
| `--discovery_addr` | string | "127.0.0.1:8100" | 🔄 重启容器 | Discovery 地址 |
| `--host` | string | "auto" | 🔄 重启容器 | 本容器 IP |
| `--heartbeat_interval` | int32 | 5 | 🔄 重启容器 | 心跳间隔 (秒) |
| `--health_check_timeout` | int32 | 2 | 🔄 重启容器 | TCP 探测超时 (秒) |
| `--fail_threshold` | int32 | 3 | 🔄 重启容器 | 连续失败次数阈值 |
| `--startup_timeout` | int32 | 30 | 🔄 重启容器 | 等待主服务就绪超时 (秒) |
