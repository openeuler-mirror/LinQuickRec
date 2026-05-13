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

/app/build/bin/proxy_server \
    --server_port="$SERVICE_PORT" \
    --discovery_addr="$DISCOVERY_ADDR" \
    --discovery_refresh_interval_ms=${DISCOVERY_REFRESH_INTERVAL_MS:-5000} \
    --downstream_max_retries=${DOWNSTREAM_MAX_RETRIES:-2} \
    --feature_service_name=${FEATURE_SERVICE_NAME:-feature_service} \
    --recall_service_name=${RECALL_SERVICE_NAME:-recall_service} \
    --precalc_service_name=${PRECALC_SERVICE_NAME:-precalc_service} \
    --rank_service_name=${RANK_SERVICE_NAME:-rank_service} \
    --feature_timeout_ms=${FEATURE_TIMEOUT_MS:-3000} \
    --recall_timeout_ms=${RECALL_TIMEOUT_MS:-5000} \
    --precalc_timeout_ms=${PRECALC_TIMEOUT_MS:-5000} \
    --rank_timeout_ms=${RANK_TIMEOUT_MS:-10000} \
    --enable_timing_stats=${ENABLE_TIMING_STATS:-true} \
    --global_thread_pool_size=${GLOBAL_THREAD_POOL_SIZE:-0} \
    "$@" &
PID_PROXY=$!

/app/build/discovery/bin/discovery_client \
    --service_type="$SERVICE_TYPE" \
    --service_port="$SERVICE_PORT" \
    --discovery_addr="$DISCOVERY_ADDR" \
    --heartbeat_interval=${HEARTBEAT_INTERVAL:-5} \
    --health_check_timeout=${HEALTH_CHECK_TIMEOUT:-2} \
    --fail_threshold=${FAIL_THRESHOLD:-3} \
    --startup_timeout=${STARTUP_TIMEOUT:-30}
EXIT_CODE=$?

kill "$PID_PROXY" 2>/dev/null || true
wait "$PID_PROXY" 2>/dev/null || true

exit $EXIT_CODE
