#!/bin/bash

# ===================== Environment Variables =====================
# worker_address: Datasystem worker address (IP:port), eg: 141.61.84.245:31501
# etcd_address:   Etcd server address (IP:port), eg: 141.61.84.245:2379
# cpu_affinity:   CPU core binding range, eg: "0-64" (env, default 0-64)
# shared_memory_size_mb: Worker shared memory size in MB (env, default 2048)
# =================================================================

# Unset proxy to avoid network interference
unset http_proxy
unset https_proxy

if [ -z "${worker_address}" ]; then
    echo "[ERROR] worker_address is not set. Please pass it via docker run -e worker_address=X.X.X.X:Y"
    exit 1
fi

if [ -z "${etcd_address}" ]; then
    echo "[ERROR] etcd_address is not set. Please pass it via docker run -e etcd_address=X.X.X.X:2379"
    exit 1
fi

if [ -z "${enable_urma}" ]; then
     echo "[ERROR] enable_urma is not set. Please set it to 0 or 1"
     exit 1
fi

ETCD_HOST="${etcd_address%%:*}"
ETCD_PORT="${etcd_address##*:}"

if ! [[ "${ETCD_HOST}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    ETCD_IP="$(getent hosts "${ETCD_HOST}" | awk '{print $1; exit}')"
    if [ -z "${ETCD_IP}" ]; then
        echo "[ERROR] failed to resolve etcd host: ${ETCD_HOST}"
        exit 1
    fi
    etcd_address="${ETCD_IP}:${ETCD_PORT}"
fi

# Default configuration
cpu_affinity="${cpu_affinity:-0-64}"

echo "=== System Info ==="
echo "Worker Address:   ${worker_address}"
echo "Etcd Addrress:    ${etcd_address}"
echo "Enable Urma:      ${enable_urma}"
echo "==================="

# Start Worker
taskset -c ${cpu_affinity} \
dscli start --worker_args \
    --worker_address "${worker_address}" \
    --etcd_address "${etcd_address}" \
    --host_id_env_name HOST_ID \
    --shared_memory_size_mb ${shared_memory_size_mb:-2048} \
    --log_dir "./datasystem_log/log_${worker_port}" \
    --arena_per_tenant 1 \
    --skip_authenticate 1 \
    --enable_urma ${enable_urma} \
    --urma_mode UB \
    --minloglevel 1 \
    --oc_thread_num 64 \
    --oc_shm_transfer_threshold_kb 0 \
    --zmq_server_io_context 16 \
    --zmq_client_io_context 16
