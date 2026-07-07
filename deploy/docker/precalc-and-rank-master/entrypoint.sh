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
echo "Starting Precalc-and-Rank-Master"
echo "==========================================="

# Build discovery flags for server
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    DISC_FLAGS="--registry_backend=etcd --etcd_endpoints=$ETCD_ENDPOINTS"
else
    DISC_FLAGS="--discovery_addr=$DISCOVERY_ADDR"
fi

cd /app/build
./bin/precalc_and_rank_master \
    --server_port=${SERVER_PORT:-8003} \
    --rank_master_server_port=${RANK_MASTER_SERVER_PORT:-8004} \
    --server_num_threads=${SERVER_NUM_THREADS:-0} \
    --server_idle_timeout_sec=${SERVER_IDLE_TIMEOUT_SEC:--1} \
    --server_max_concurrency=${SERVER_MAX_CONCURRENCY:-0} \
    $DISC_FLAGS \
    --discovery_refresh_interval_ms=${DISCOVERY_REFRESH_INTERVAL_MS:-5000} \
    --kv_worker_service=${KV_WORKER_SERVICE:-kv_worker} \
    --precalc_sleep_time_ms=${PRECALC_SLEEP_TIME_MS:-30} \
    --ttl_seconds=${TTL_SECONDS:-5} \
    --precalc_result_size_mb=${PRECALC_RESULT_SIZE_MB:-8.5} \
    --payload_size_kb=${PAYLOAD_SIZE_KB:-100} \
    --sub_worker_timeout_ms=${SUB_WORKER_TIMEOUT_MS:-5000} \
    --sub_worker_service_type=${SUB_WORKER_SERVICE_TYPE:-rank_sub} \
    --sub_worker_connection_type=${SUB_WORKER_CONNECTION_TYPE:-pooled} \
    --sub_worker_max_retry=${SUB_WORKER_MAX_RETRY:-3} \
    --sub_worker_connect_timeout_ms=${SUB_WORKER_CONNECT_TIMEOUT_MS:--1} \
    --sub_worker_backup_request_ms=${SUB_WORKER_BACKUP_REQUEST_MS:--1} \
    --rank_master_sleep_time_ms=${RANK_MASTER_SLEEP_TIME_MS:-30} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
# Precalc service registration
/app/discovery_client \
    --service_type=precalc_service \
    --service_port=${SERVER_PORT:-8003} \
    $DISC_FLAGS \
    --host=${HOST:-auto} \
    --heartbeat_interval=${HEARTBEAT_INTERVAL:-5} \
    --health_check_timeout=${HEALTH_CHECK_TIMEOUT:-2} \
    --fail_threshold=${FAIL_THRESHOLD:-3} \
    --startup_timeout=${STARTUP_TIMEOUT:-30} &
PID1=$!

# Rank service registration
/app/discovery_client \
    --service_type=rank_service \
    --service_port=${RANK_MASTER_SERVER_PORT:-8004} \
    $DISC_FLAGS \
    --host=${HOST:-auto} \
    --heartbeat_interval=${HEARTBEAT_INTERVAL:-5} \
    --health_check_timeout=${HEALTH_CHECK_TIMEOUT:-2} \
    --fail_threshold=${FAIL_THRESHOLD:-3} \
    --startup_timeout=${STARTUP_TIMEOUT:-30} &
PID2=$!

echo "All services started, waiting for any process to exit..."
wait -n $SERVICE_PID $PID1 $PID2
