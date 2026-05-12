#!/bin/sh
set -e

if [ -z "$SERVICE_TYPE" ] || [ -z "$SERVICE_PORT" ]; then
    echo "ERROR: SERVICE_TYPE and SERVICE_PORT environment variables are required"
    echo "Usage: docker run -e SERVICE_TYPE=recall_service -e SERVICE_PORT=8001 ..."
    exit 1
fi

DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"

echo "========================================"
echo "Pseudo service starting"
echo "  service_type: $SERVICE_TYPE"
echo "  service_port: $SERVICE_PORT"
echo "  discovery_addr: $DISCOVERY_ADDR"
echo "========================================"

pseudo_service --port "$SERVICE_PORT" &
PID_SERVICE=$!

discovery_client \
    --service_type="$SERVICE_TYPE" \
    --service_port="$SERVICE_PORT" \
    --discovery_addr="$DISCOVERY_ADDR"
EXIT_CODE=$?

kill "$PID_SERVICE" 2>/dev/null || true
wait "$PID_SERVICE" 2>/dev/null || true

exit $EXIT_CODE
