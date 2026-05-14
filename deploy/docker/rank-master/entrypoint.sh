#!/bin/bash
set -e

echo "==========================================="
echo "Starting RankMaster Service"
echo "==========================================="

cd /app/build
./bin/rank_master_server \
    --server_port=${SERVER_PORT:-8004} \
    --server_num_threads=${SERVER_NUM_THREADS:-0} \
    --server_timeout_ms=${SERVER_TIMEOUT_MS:-0} \
    --server_idle_timeout_sec=${SERVER_IDLE_TIMEOUT_SEC:--1} \
    --server_max_concurrency=${SERVER_MAX_CONCURRENCY:-0} \
    --top_k=${TOP_K:-100} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100} \
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
    --service_type=rank_master \
    --service_port=${SERVER_PORT:-8004} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100} \
    --discovery_client_timeout_ms=${DISCOVERY_CLIENT_TIMEOUT_MS:-5000} \
    --discovery_client_connection_type=${DISCOVERY_CLIENT_CONNECTION_TYPE:-single} \
    --discovery_client_max_retry=${DISCOVERY_CLIENT_MAX_RETRY:-2} \
    --discovery_client_connect_timeout_ms=${DISCOVERY_CLIENT_CONNECT_TIMEOUT_MS:--1} \
    --discovery_client_backup_request_ms=${DISCOVERY_CLIENT_BACKUP_REQUEST_MS:--1}
EXIT_CODE=$?

kill "$SERVICE_PID" 2>/dev/null || true
wait "$SERVICE_PID" 2>/dev/null || true

exit $EXIT_CODE
