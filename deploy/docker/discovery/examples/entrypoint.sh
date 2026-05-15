#!/bin/sh
set -e

if [ -z "$SERVICE_TYPE" ] || [ -z "$SERVICE_PORT" ]; then
    echo "ERROR: SERVICE_TYPE and SERVICE_PORT environment variables are required"
    echo "Usage: docker run -e SERVICE_TYPE=recall_service -e SERVICE_PORT=8001 ..."
    exit 1
fi

DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"
ETCD_ENDPOINTS="${ETCD_ENDPOINTS:-etcd:2379}"
REGISTRY_BACKEND="${REGISTRY_BACKEND:-discovery_server}"

echo "========================================"
echo "Pseudo service starting"
echo "  service_type: $SERVICE_TYPE"
echo "  service_port: $SERVICE_PORT"
echo "  registry_backend: $REGISTRY_BACKEND"

if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    echo "  etcd_endpoints: $ETCD_ENDPOINTS"
else
    echo "  discovery_addr: $DISCOVERY_ADDR"
fi
echo "========================================"

pseudo_service --port "$SERVICE_PORT" &
PID_SERVICE=$!

if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    discovery_client \
        --service_type="$SERVICE_TYPE" \
        --service_port="$SERVICE_PORT" \
        --registry_backend=etcd \
        --etcd_endpoints="$ETCD_ENDPOINTS"
else
    discovery_client \
        --service_type="$SERVICE_TYPE" \
        --service_port="$SERVICE_PORT" \
        --discovery_addr="$DISCOVERY_ADDR"
fi
EXIT_CODE=$?

kill "$PID_SERVICE" 2>/dev/null || true
wait "$PID_SERVICE" 2>/dev/null || true

exit $EXIT_CODE
