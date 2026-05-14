#!/bin/bash
set -e

echo "==========================================="
echo "Starting Precalc Service"
echo "==========================================="

cd /app/build
./bin/precalc_server \
    --server_port=${SERVER_PORT:-8003} \
    --server_num_threads=${SERVER_NUM_THREADS:-0} \
    --server_timeout_ms=${SERVER_TIMEOUT_MS:-0} \
    --server_idle_timeout_sec=${SERVER_IDLE_TIMEOUT_SEC:--1} \
    --server_max_concurrency=${SERVER_MAX_CONCURRENCY:-0} \
    --kvworker_host=${KVWORKER_HOST:-141.61.84.245} \
    --kvworker_port=${KVWORKER_PORT:-31502} \
    --etcd_address=${ETCD_ADDRESS:-141.61.84.245:2379} \
    --ttl_seconds=${TTL_SECONDS:-5} \
    --precalc_result_size_mb=${PRECALC_RESULT_SIZE_MB:-8.5} \
    --payload_size_kb=${PAYLOAD_SIZE_KB:-100} \
    --global_thread_pool_size=${GLOBAL_THREAD_POOL_SIZE:-0} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
/app/discovery_client \
    --service_type=precalc_service \
    --service_port=${SERVER_PORT:-8003} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100} \
    --discovery_client_timeout_ms=${DISCOVERY_CLIENT_TIMEOUT_MS:-5000} \
    --discovery_client_connection_type=${DISCOVERY_CLIENT_CONNECTION_TYPE:-single} \
    --discovery_client_max_retry=${DISCOVERY_CLIENT_MAX_RETRY:-2} \
    --discovery_client_connect_timeout_ms=${DISCOVERY_CLIENT_CONNECT_TIMEOUT_MS:--1} \
    --discovery_client_backup_request_ms=${DISCOVERY_CLIENT_BACKUP_REQUEST_MS:--1} &
DISCOVERY_PID=$!

echo "All services started, waiting for any process to exit..."
wait -n $SERVICE_PID $DISCOVERY_PID
