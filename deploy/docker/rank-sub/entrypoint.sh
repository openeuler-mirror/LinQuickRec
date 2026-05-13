#!/bin/bash
set -e

echo "==========================================="
echo "Starting RankSub Service"
echo "==========================================="

cd /app/build
./bin/rank_sub_server \
    --server_port=${SERVER_PORT:-8005} \
    --kvworker_host=${KVWORKER_HOST:-141.61.84.245} \
    --kvworker_port=${KVWORKER_PORT:-31502} \
    --etcd_address=${ETCD_ADDRESS:-141.61.84.245:2379} \
    --scoring_delay_ms=${SCORING_DELAY_MS:-100} \
    --global_thread_pool_size=${GLOBAL_THREAD_POOL_SIZE:-0} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
/app/discovery_client \
    --service_type=rank_sub \
    --service_port=${SERVER_PORT:-8005} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100} &
DISCOVERY_PID=$!

echo "All services started, waiting for any process to exit..."
wait -n $SERVICE_PID $DISCOVERY_PID
