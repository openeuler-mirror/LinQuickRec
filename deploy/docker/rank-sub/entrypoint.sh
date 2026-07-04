#!/bin/bash
set -e

REGISTRY_BACKEND="${REGISTRY_BACKEND:-discovery_server}"
ETCD_ENDPOINTS="${ETCD_ENDPOINTS:-etcd:2379}"
DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"

# 元戎 SDK 不接受 DNS hostname，需解析为 IP
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    ETCD_HOST="${ETCD_ENDPOINTS%%:*}"
    ETCD_PORT="${ETCD_ENDPOINTS##*:}"
    ETCD_IP=$(getent hosts "$ETCD_HOST" | head -1 | awk '{print $1}')
    ETCD_ENDPOINTS="${ETCD_IP}:${ETCD_PORT}"
fi

echo "==========================================="
echo "Starting RankSub Service"
echo "==========================================="

# Build discovery flags for rank_sub_server
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    DISCOVERY_FLAGS="--registry_backend=etcd --etcd_endpoints=http://$ETCD_ENDPOINTS"
else
    DISCOVERY_FLAGS="--discovery_addr=$DISCOVERY_ADDR"
fi

cd /app/build
./bin/rank_sub_server \
    --server_port=${SERVER_PORT:-8005} \
    --server_num_threads=${SERVER_NUM_THREADS:-0} \
    --server_idle_timeout_sec=${SERVER_IDLE_TIMEOUT_SEC:--1} \
    --server_max_concurrency=${SERVER_MAX_CONCURRENCY:-0} \
    $DISCOVERY_FLAGS \
    --kv_worker_service=${KV_WORKER_SERVICE:-kv_worker} \
    --rank_sub_sleep_time_ms=${RANK_SUB_SLEEP_TIME_MS:-30} \
    --rank_sub_payload_size_kb=${RANK_SUB_PAYLOAD_SIZE_KB:-0} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    DISCOVERY_FLAGS="--registry_backend=etcd --etcd_endpoints=$ETCD_ENDPOINTS"
else
    DISCOVERY_FLAGS="--discovery_addr=$DISCOVERY_ADDR"
fi
/app/discovery_client \
    --service_type=rank_sub \
    --service_port=${SERVER_PORT:-8005} \
    $DISCOVERY_FLAGS \
    --host=${HOST:-auto} \
    --heartbeat_interval=${HEARTBEAT_INTERVAL:-5} \
    --health_check_timeout=${HEALTH_CHECK_TIMEOUT:-2} \
    --fail_threshold=${FAIL_THRESHOLD:-3} \
    --startup_timeout=${STARTUP_TIMEOUT:-30} &
DISCOVERY_PID=$!

echo "All services started, waiting for any process to exit..."
wait -n $SERVICE_PID $DISCOVERY_PID
