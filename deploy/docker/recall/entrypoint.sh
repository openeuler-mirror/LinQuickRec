#!/bin/bash
set -e

REGISTRY_BACKEND="${REGISTRY_BACKEND:-discovery_server}"
ETCD_ENDPOINTS="${ETCD_ENDPOINTS:-etcd:2379}"
DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"
ENABLE_VLLM="${ENABLE_VLLM:-true}"

echo "==========================================="
echo "Starting Recall Service"
echo "  enable_vllm=${ENABLE_VLLM}"
echo "==========================================="

if [ "$ENABLE_VLLM" = "true" ]; then
    echo "Starting vLLM..."
    /app/start_vllm_back.sh &
    VLLM_PID=$!

    echo "Waiting for vLLM to be ready..."
    for i in $(seq 1 ${VLLM_STARTUP_TIMEOUT:-120}); do
        if curl -s http://127.0.0.1:${VLLM_PORT:-8000}/health > /dev/null 2>&1; then
            echo "vLLM is ready!"
            break
        fi
        if [ $i -eq ${VLLM_STARTUP_TIMEOUT:-120} ]; then
            echo "vLLM failed to start within ${VLLM_STARTUP_TIMEOUT:-120} seconds"
            exit 1
        fi
        sleep 1
    done
else
    echo "vLLM disabled, skipping startup."
fi

echo "Starting Recall Service..."
cd /app/build

# KVCache 模式下透传服务发现参数
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    KV_DISCOVERY_FLAGS="--registry_backend=etcd --etcd_endpoints=${ETCD_ENDPOINTS}"
else
    KV_DISCOVERY_FLAGS="--registry_backend=discovery_server --discovery_addr=${DISCOVERY_ADDR}"
fi

./bin/recall_server \
    --server_port=${SERVER_PORT:-8002} \
    --server_num_threads=${SERVER_NUM_THREADS:-0} \
    --server_idle_timeout_sec=${SERVER_IDLE_TIMEOUT_SEC:--1} \
    --server_max_concurrency=${SERVER_MAX_CONCURRENCY:-0} \
    --recall_sleep_time_ms=${RECALL_SLEEP_TIME_MS:-30} \
    --recall_payload_size_kb=${RECALL_PAYLOAD_SIZE_KB:-0} \
    --enable_vllm=${ENABLE_VLLM} \
    --vllm_base_url=${VLLM_BASE_URL:-http://127.0.0.1:8000} \
    --vllm_endpoint=${VLLM_ENDPOINT:-/v1/chat/completions} \
    --model_name=${MODEL_NAME:-/app/models/Qwen3-0.6B/} \
    --vllm_timeout_ms=${VLLM_TIMEOUT_MS:-100000} \
    --vllm_connection_type=${VLLM_CONNECTION_TYPE:-pooled} \
    --vllm_max_retry=${VLLM_MAX_RETRY:-3} \
    --vllm_connect_timeout_ms=${VLLM_CONNECT_TIMEOUT_MS:--1} \
    --vllm_backup_request_ms=${VLLM_BACKUP_REQUEST_MS:--1} \
    --sku_count=${SKU_COUNT:-1000} \
    --kv_worker_service=${KV_WORKER_SERVICE:-kv_worker} \
    --kvcache_ttl_seconds=${KVCACHE_TTL_SECONDS:-3600} \
    --kvcache_size_bytes=${KVCACHE_SIZE_BYTES:-256} \
    $KV_DISCOVERY_FLAGS \
    "$@" &
RECALL_PID=$!

echo "Starting Discovery Client..."
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    DISCOVERY_FLAGS="--registry_backend=etcd --etcd_endpoints=$ETCD_ENDPOINTS"
else
    DISCOVERY_FLAGS="--discovery_addr=$DISCOVERY_ADDR"
fi
/app/discovery_client \
    --service_type=recall_service \
    --service_port=${SERVER_PORT:-8002} \
    $DISCOVERY_FLAGS \
    --host=${HOST:-auto} \
    --heartbeat_interval=${HEARTBEAT_INTERVAL:-5} \
    --health_check_timeout=${HEALTH_CHECK_TIMEOUT:-2} \
    --fail_threshold=${FAIL_THRESHOLD:-3} \
    --startup_timeout=${STARTUP_TIMEOUT:-30} &
DISCOVERY_PID=$!

echo "All services started, waiting for recall/discovery to exit..."
wait -n $RECALL_PID $DISCOVERY_PID
