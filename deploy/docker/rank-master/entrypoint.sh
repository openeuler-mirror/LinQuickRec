#!/bin/bash
set -e

echo "==========================================="
echo "Starting RankMaster Service"
echo "==========================================="

cd /app/build
./bin/rank_master_server \
    --server_port=${SERVER_PORT:-8004} \
    --top_k=${TOP_K:-100} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100} \
    --sub_worker_service_type=${SUB_WORKER_SERVICE_TYPE:-rank_sub} \
    --global_thread_pool_size=${GLOBAL_THREAD_POOL_SIZE:-0} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
/app/discovery_client \
    --service_type=rank_master \
    --service_port=${SERVER_PORT:-8004} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100}
EXIT_CODE=$?

kill "$SERVICE_PID" 2>/dev/null || true
wait "$SERVICE_PID" 2>/dev/null || true

exit $EXIT_CODE
