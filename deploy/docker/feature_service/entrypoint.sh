#!/bin/bash
set -e

echo "==========================================="
echo "Starting Feature Service"
echo "==========================================="

exec /app/build/bin/feature_server \
    --server_port=${SERVER_PORT:-8003} \
    "$@"
