#!/bin/bash
set -e

echo "==========================================="
echo "Starting Discovery Server"
echo "==========================================="

exec /app/build/bin/discovery_server \
    --server_port=${SERVER_PORT:-8100} \
    --heartbeat_check_interval_ms=${HEARTBEAT_CHECK_INTERVAL_MS:-1000} \
    --heartbeat_grace_factor=${HEARTBEAT_GRACE_FACTOR:-2.0} \
    --cleanup_factor=${CLEANUP_FACTOR:-5.0} \
    "$@"
