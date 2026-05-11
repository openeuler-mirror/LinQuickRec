#!/bin/bash
set -e

echo "==========================================="
echo "Starting Feature Service (mock)"
echo "==========================================="

cd /app/build
./feature_server \
    --server_port=${SERVER_PORT:-8003} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
/app/discovery_client \
    --service_type=feature_service \
    --service_port=${SERVER_PORT:-8003} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100} &
DISCOVERY_PID=$!

echo "All services started, waiting for any process to exit..."
wait -n $SERVICE_PID $DISCOVERY_PID
