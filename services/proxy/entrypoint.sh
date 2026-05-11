#!/bin/sh
set -e

SERVICE_TYPE="${SERVICE_TYPE:-proxy}"
SERVICE_PORT="${SERVICE_PORT:-8080}"
DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"

/app/build/proxy_server --server_port="$SERVICE_PORT" "$@" &
PID_PROXY=$!

/app/build/discovery/bin/discovery_client \
    --service_type="$SERVICE_TYPE" \
    --service_port="$SERVICE_PORT" \
    --discovery_addr="$DISCOVERY_ADDR"
EXIT_CODE=$?

kill "$PID_PROXY" 2>/dev/null || true
wait "$PID_PROXY" 2>/dev/null || true

exit $EXIT_CODE
