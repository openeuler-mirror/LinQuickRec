#!/bin/sh
set -e

if [ "$1" = "test" ]; then
    shift
    exec /app/build/bin/proxy_integration_test "$@"
fi

SERVICE_TYPE="${SERVICE_TYPE:-proxy}"
SERVICE_PORT="${SERVICE_PORT:-8080}"

# 从命令行参数中提取 discovery_addr（若存在），否则使用环境变量默认值
DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"
for arg in "$@"; do
    case "$arg" in
        --discovery_addr=*) DISCOVERY_ADDR="${arg#*=}";;
    esac
done

/app/build/bin/proxy_server --server_port="$SERVICE_PORT" "$@" &
PID_PROXY=$!

/app/build/discovery/bin/discovery_client \
    --service_type="$SERVICE_TYPE" \
    --service_port="$SERVICE_PORT" \
    --discovery_addr="$DISCOVERY_ADDR"
EXIT_CODE=$?

kill "$PID_PROXY" 2>/dev/null || true
wait "$PID_PROXY" 2>/dev/null || true

exit $EXIT_CODE
