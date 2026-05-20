#!/bin/bash
set -e

REGISTRY_BACKEND="${REGISTRY_BACKEND:-discovery_server}"
ETCD_ENDPOINTS="${ETCD_ENDPOINTS:-etcd:2379}"
DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"

echo "==========================================="
echo "Starting RankMaster Service"
echo "==========================================="

# Build discovery flags for rank_master_server
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    DISCOVERY_FLAGS="--registry_backend=etcd --etcd_endpoints=$ETCD_ENDPOINTS"
else
    DISCOVERY_FLAGS="--discovery_addr=$DISCOVERY_ADDR"
fi

cd /app/build
./bin/rank_master_server \
    --server_port=${SERVER_PORT:-8004} \
    --server_num_threads=${SERVER_NUM_THREADS:-0} \
    --server_idle_timeout_sec=${SERVER_IDLE_TIMEOUT_SEC:--1} \
    --server_max_concurrency=${SERVER_MAX_CONCURRENCY:-0} \
    --top_k=${TOP_K:-100} \
    $DISCOVERY_FLAGS \
    --sub_worker_service_type=${SUB_WORKER_SERVICE_TYPE:-rank_sub} \
    --sub_worker_parallelism=${SUB_WORKER_PARALLELISM:-4} \
    --sub_worker_connection_type=${SUB_WORKER_CONNECTION_TYPE:-pooled} \
    --sub_worker_max_retry=${SUB_WORKER_MAX_RETRY:-3} \
    --sub_worker_connect_timeout_ms=${SUB_WORKER_CONNECT_TIMEOUT_MS:--1} \
    --sub_worker_backup_request_ms=${SUB_WORKER_BACKUP_REQUEST_MS:--1} \
    --sub_worker_timeout_ms=${SUB_WORKER_TIMEOUT_MS:-5000} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
/app/discovery_client \
    --service_type=rank_service \
    --service_port=${SERVER_PORT:-8004} \
    $DISCOVERY_FLAGS \
    --host=${HOST:-auto} \
    --heartbeat_interval=${HEARTBEAT_INTERVAL:-5} \
    --health_check_timeout=${HEALTH_CHECK_TIMEOUT:-2} \
    --fail_threshold=${FAIL_THRESHOLD:-3} \
    --startup_timeout=${STARTUP_TIMEOUT:-30}
EXIT_CODE=$?

kill "$SERVICE_PID" 2>/dev/null || true
wait "$SERVICE_PID" 2>/dev/null || true

exit $EXIT_CODE
