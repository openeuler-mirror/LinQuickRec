#!/bin/bash
set -e

echo "==========================================="
echo "Starting Recall Service + vLLM"
echo "==========================================="

echo "Starting vLLM..."
/app/run_vllm.sh &
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
./recall_server \
    --server_port=${SERVER_PORT:-8001} \
    --vllm_base_url=${VLLM_BASE_URL:-http://127.0.0.1:8000} \
    --vllm_endpoint=${VLLM_ENDPOINT:-/v1/chat/completions} \
    --model_name=${MODEL_NAME:-/app/models/Qwen3-0.6B/} \
    --vllm_timeout_ms=${VLLM_TIMEOUT_MS:-5000} \
    --sku_count=${SKU_COUNT:-1000} \
    "$@"

kill $VLLM_PID 2>/dev/null
