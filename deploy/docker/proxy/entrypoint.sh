#!/bin/bash
set -e

echo "==========================================="
echo "Starting Proxy Service"
echo "==========================================="

cd /app/build
./gateway_server \
    --server_port=${SERVER_PORT:-8080} \
    --feature_service_addr=${FEATURE_SERVICE_ADDR:-feature-service:8003} \
    --recall_service_addr=${RECALL_SERVICE_ADDR:-recall-service:8001} \
    --precalc_service_addr=${PRECALC_SERVICE_ADDR:-precalc-service:8004} \
    --rank_service_addr=${RANK_SERVICE_ADDR:-rank-master-service:8005} \
    "$@" &
SERVICE_PID=$!

echo "Starting Discovery Client..."
/app/discovery_client \
    --service_type=proxy \
    --service_port=${SERVER_PORT:-8080} \
    --discovery_addr=${DISCOVERY_ADDR:-discovery-server:8100} &
DISCOVERY_PID=$!

echo "All services started, waiting for any process to exit..."
wait -n $SERVICE_PID $DISCOVERY_PID
