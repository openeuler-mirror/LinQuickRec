#!/bin/bash
set -e

REGISTRY_BACKEND="${REGISTRY_BACKEND:-discovery_server}"
ETCD_ENDPOINTS="${ETCD_ENDPOINTS:-etcd:2379}"
DISCOVERY_ADDR="${DISCOVERY_ADDR:-discovery-server:8100}"

# 元戎 SDK 不接受 DNS hostname，需解析为 IP
if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    ETCD_HOST="${ETCD_ENDPOINTS%%:*}"
    ETCD_PORT="${ETCD_ENDPOINTS##*:}"
    ETCD_IP=$(getent hosts "$ETCD_HOST" | head -1 | awk '{print $1}')
    if [ -z "$ETCD_IP" ]; then
        echo "[ERROR] failed to resolve etcd host: $ETCD_HOST"
        exit 1
    fi
    ETCD_ENDPOINTS="${ETCD_IP}:${ETCD_PORT}"
fi

# etcd_address 供元戎 SDK 使用，同样需要解析主机名为 IP
if [ -n "${etcd_address}" ]; then
    ETCD_ADDR_HOST="${etcd_address%%:*}"
    ETCD_ADDR_PORT="${etcd_address##*:}"
    if [[ "${ETCD_ADDR_HOST}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        ETCD_ADDR_IP="$ETCD_ADDR_HOST"
    else
        ETCD_ADDR_IP=$(getent hosts "$ETCD_ADDR_HOST" | head -1 | awk '{print $1}')
        if [ -z "$ETCD_ADDR_IP" ]; then
            echo "[ERROR] failed to resolve etcd host: $ETCD_ADDR_HOST"
            exit 1
        fi
    fi
    export etcd_address="${ETCD_ADDR_IP}:${ETCD_ADDR_PORT}"
fi

echo "==========================================="
echo "Starting KV Worker"
echo "==========================================="

WORKER_HOST="${HOST:-auto}"
if [[ "${worker_address}" == *:* ]]; then
    WORKER_PORT="${worker_address##*:}"
else
    WORKER_PORT="31501"
fi

# 覆盖 worker_address 使 datasystem 监听所有接口，确保 127.0.0.1 健康检查可达
export worker_address="0.0.0.0:${WORKER_PORT}"
/workerspace/start_datasystem.sh &
WORKER_PID=$!

if [ "$REGISTRY_BACKEND" = "etcd" ]; then
    DISCOVERY_FLAGS="--registry_backend=etcd --etcd_endpoints=$ETCD_ENDPOINTS"
else
    DISCOVERY_FLAGS="--discovery_addr=$DISCOVERY_ADDR"
fi

echo "Starting Discovery Client..."
/app/discovery_client \
    --service_type=kv_worker \
    --service_port=$WORKER_PORT \
    $DISCOVERY_FLAGS \
    --host=$WORKER_HOST \
    --heartbeat_interval=${HEARTBEAT_INTERVAL:-5} \
    --health_check_timeout=${HEALTH_CHECK_TIMEOUT:-2} \
    --fail_threshold=${FAIL_THRESHOLD:-3} \
    --startup_timeout=${STARTUP_TIMEOUT:-30}
EXIT_CODE=$?

kill "$WORKER_PID" 2>/dev/null || true
wait "$WORKER_PID" 2>/dev/null || true

exit $EXIT_CODE
