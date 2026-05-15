#!/bin/bash
set -e

REGISTRY_BACKEND="${REGISTRY_BACKEND:-discovery_server}"
ETCD_ENDPOINTS="${ETCD_ENDPOINTS:-etcd:2379}"
DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"

echo "==========================================="
echo "Starting Feature Service (mock)"
echo "==========================================="

cd /app/build
./bin/feature_server \
    --server_port=${SERVER_PORT:-8001} \
    --server_num_threads=${SERVER_NUM_THREADS:-0} \
    --server_idle_timeout_sec=${SERVER_IDLE_TIMEOUT_SEC:--1} \
    --server_max_concurrency=${SERVER_MAX_CONCURRENCY:-0} \
    --user_log_count=${USER_LOG_COUNT:-10} \
    --user_log_vec_size=${USER_LOG_VEC_SIZE:-30} \
    --sku_feat_length=${SKU_FEAT_LENGTH:-20} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    DISCOVERY_FLAGS="--registry_backend=etcd --etcd_endpoints=$ETCD_ENDPOINTS"
else
    DISCOVERY_FLAGS="--discovery_addr=$DISCOVERY_ADDR"
fi
/app/discovery_client \
    --service_type=feature_service \
    --service_port=${SERVER_PORT:-8001} \
    $DISCOVERY_FLAGS \
    --host=${HOST:-auto} \
    --heartbeat_interval=${HEARTBEAT_INTERVAL:-5} \
    --health_check_timeout=${HEALTH_CHECK_TIMEOUT:-2} \
    --fail_threshold=${FAIL_THRESHOLD:-3} \
    --startup_timeout=${STARTUP_TIMEOUT:-30} &
DISCOVERY_PID=$!

echo "All services started, waiting for any process to exit..."
wait -n $SERVICE_PID $DISCOVERY_PID
