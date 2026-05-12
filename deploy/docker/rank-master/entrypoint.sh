#!/bin/bash
set -e

echo "==========================================="
echo "Starting RankMaster Service"
echo "==========================================="

RANK_SUB_HOST=${RANK_SUB_HOST:-rank-sub-service}
RANK_SUB_PORT=${RANK_SUB_PORT:-8005}

echo "Waiting for ${RANK_SUB_HOST}:${RANK_SUB_PORT} to be ready..."
for i in $(seq 1 ${RANK_SUB_STARTUP_TIMEOUT:-120}); do
    if nc -z ${RANK_SUB_HOST} ${RANK_SUB_PORT} 2>/dev/null; then
        echo "rank-sub-service is ready!"
        break
    fi
    if [ $i -eq ${RANK_SUB_STARTUP_TIMEOUT:-120} ]; then
        echo "rank-sub-service not available, starting anyway..."
        break
    fi
    sleep 1
done

cd /app/build
./rank_master_server \
    --server_port=${SERVER_PORT:-8004} \
    --sub_worker_count=${SUB_WORKER_COUNT:-3} \
    --sub_worker_addresses=${SUB_WORKER_ADDRESSES:-rank-sub-service:8005} \
    --top_k=${TOP_K:-100} \
    --enable_timing_stats=${ENABLE_TIMING_STATS:-true} \
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
