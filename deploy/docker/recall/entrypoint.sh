#!/bin/bash
set -e

echo "==========================================="
echo "Starting Recall Service + vLLM"
echo "==========================================="

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

echo "Starting Recall Service..."
cd /app/build
./bin/recall_server \
    --server_port=${SERVER_PORT:-8002} \
    --server_num_threads=${SERVER_NUM_THREADS:-0} \
    --server_timeout_ms=${SERVER_TIMEOUT_MS:-0} \
    --server_idle_timeout_sec=${SERVER_IDLE_TIMEOUT_SEC:--1} \
    --server_max_concurrency=${SERVER_MAX_CONCURRENCY:-0} \
    --vllm_base_url=${VLLM_BASE_URL:-http://127.0.0.1:8000} \
    --vllm_endpoint=${VLLM_ENDPOINT:-/v1/chat/completions} \
    --model_name=${MODEL_NAME:-/app/models/Qwen3-0.6B/} \
    --vllm_timeout_ms=${VLLM_TIMEOUT_MS:-100000} \
    --vllm_connection_type=${VLLM_CONNECTION_TYPE:-single} \
    --vllm_max_retry=${VLLM_MAX_RETRY:-3} \
    --vllm_connect_timeout_ms=${VLLM_CONNECT_TIMEOUT_MS:--1} \
    --vllm_backup_request_ms=${VLLM_BACKUP_REQUEST_MS:--1} \
    --sku_count=${SKU_COUNT:-100} \
    "$@" &
RECALL_PID=$!

echo "Starting Discovery Client..."
/app/discovery_client \
    --service_type=recall_service \
    --service_port=${SERVER_PORT:-8002} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100} \
    --discovery_client_timeout_ms=${DISCOVERY_CLIENT_TIMEOUT_MS:-5000} \
    --discovery_client_connection_type=${DISCOVERY_CLIENT_CONNECTION_TYPE:-single} \
    --discovery_client_max_retry=${DISCOVERY_CLIENT_MAX_RETRY:-2} \
    --discovery_client_connect_timeout_ms=${DISCOVERY_CLIENT_CONNECT_TIMEOUT_MS:--1} \
    --discovery_client_backup_request_ms=${DISCOVERY_CLIENT_BACKUP_REQUEST_MS:--1} &
DISCOVERY_PID=$!

echo "All services started, waiting for recall/discovery to exit..."
wait -n $RECALL_PID $DISCOVERY_PID
