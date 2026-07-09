#!/bin/bash
set -e

REGISTRY_BACKEND="${REGISTRY_BACKEND:-etcd}"
ETCD_ENDPOINTS="${ETCD_ENDPOINTS:-etcd-client:2379}"
DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"

echo "==========================================="
echo "Starting Perf-collector"
echo "==========================================="

mkdir -p /var/lib/perf

cd /app/build
./bin/perf_collector \
    --server_port=${SERVER_PORT:-8080} \
    --registry_backend="${REGISTRY_BACKEND}" \
    --etcd_endpoints="${ETCD_ENDPOINTS}" \
    --discovery_addr="${DISCOVERY_ADDR}" \
    --sqlite_db_path="${SQLITE_DB_PATH:-/var/lib/perf/perf.db}" \
    --retention_days=${RETENTION_DAYS:-30} \
    "$@"
