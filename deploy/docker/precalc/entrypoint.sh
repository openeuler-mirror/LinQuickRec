#!/bin/bash
set -e

echo "==========================================="
echo "Starting Precalc Service"
echo "==========================================="

cd /app/build
./precalc_server \
    --server_port=${SERVER_PORT:-8004} \
    --kvworker_host=${KVWORKER_HOST:-141.61.84.245} \
    --kvworker_port=${KVWORKER_PORT:-31501} \
    --etcd_address=${ETCD_ADDRESS:-141.61.84.245:2379} \
    --ttl_seconds=${TTL_SECONDS:-5} \
    --precalc_result_size_mb=${PRECALC_RESULT_SIZE_MB:-8.5} \
    --payload_size_kb=${PAYLOAD_SIZE_KB:-100} \
    "$@"
