#!/bin/bash
set -e

DATA_DIR="${DATA_DIR:-/var/lib/etcd}"
CLUSTER_SIZE="${CLUSTER_SIZE:-5}"
SERVICE="${SERVICE_NAME:-etcd}"
NS="${CLUSTER_NS:-linquickrec}"

mkdir -p "$DATA_DIR"

MY_DNS="${HOSTNAME}.${SERVICE}.${NS}.svc.cluster.local"
ETCD0_DNS="etcd-0.${SERVICE}.${NS}.svc.cluster.local"

logger() { echo "[$(date +%H:%M:%S)] $*"; }

# --- Restart (data dir has previous state) ---
if [ -d "$DATA_DIR/member/snap" ]; then
    logger "Restart detected, joining existing cluster as ${HOSTNAME}"
    exec etcd \
      --name "$HOSTNAME" \
      --data-dir "$DATA_DIR" \
      --listen-client-urls http://0.0.0.0:2379 \
      --advertise-client-urls "http://${MY_DNS}:2379" \
      --listen-peer-urls http://0.0.0.0:2380 \
      --initial-advertise-peer-urls "http://${MY_DNS}:2380" \
      --initial-cluster-token "linquickrec-etcd" \
      --initial-cluster "${ETCD0_DNS}=http://${ETCD0_DNS}:2380" \
      --initial-cluster-state "existing"
fi

# --- Bootstrap: etcd-0 ---
if [ "$HOSTNAME" = "etcd-0" ]; then
    logger "Bootstrap: starting etcd-0 as single-node cluster"

    etcd \
      --name etcd-0 \
      --data-dir "$DATA_DIR" \
      --listen-client-urls http://0.0.0.0:2379 \
      --advertise-client-urls "http://${ETCD0_DNS}:2379" \
      --listen-peer-urls http://0.0.0.0:2380 \
      --initial-advertise-peer-urls "http://${ETCD0_DNS}:2380" \
      --initial-cluster-token "linquickrec-etcd" \
      --initial-cluster "etcd-0=http://${ETCD0_DNS}:2380" \
      --initial-cluster-state "new" &
    ETCD_PID=$!

    logger "Waiting for etcd-0 to be ready..."
    for i in $(seq 1 30); do
        if curl -fsS "http://127.0.0.1:2379/health" >/dev/null 2>&1; then
            break
        fi
        sleep 1
    done
    logger "etcd-0 is healthy"

    for i in $(seq 1 $((CLUSTER_SIZE - 1))); do
        PEER="etcd-${i}.${SERVICE}.${NS}.svc.cluster.local"
        logger "Adding member etcd-${i} (${PEER}:2380)"
        etcdctl member add "etcd-${i}" --peer-urls="http://${PEER}:2380"
    done
    logger "All ${CLUSTER_SIZE} members added, cluster ready"

    wait $ETCD_PID
fi

# --- Join: etcd-1~N ---
logger "Joining: waiting for etcd-0 to be reachable..."
for i in $(seq 1 60); do
    if curl -fsS "http://${ETCD0_DNS}:2379/health" >/dev/null 2>&1; then
        break
    fi
    [ "$i" -eq 60 ] && { logger "ERROR: etcd-0 not reachable after 60s"; exit 1; }
    sleep 1
done

# Give etcd-0 a few seconds to finish member-add
sleep 5

logger "Starting ${HOSTNAME} as cluster member"
exec etcd \
  --name "$HOSTNAME" \
  --data-dir "$DATA_DIR" \
  --listen-client-urls http://0.0.0.0:2379 \
  --advertise-client-urls "http://${MY_DNS}:2379" \
  --listen-peer-urls http://0.0.0.0:2380 \
  --initial-advertise-peer-urls "http://${MY_DNS}:2380" \
  --initial-cluster-token "linquickrec-etcd" \
  --initial-cluster "${ETCD0_DNS}=http://${ETCD0_DNS}:2380" \
  --initial-cluster-state "existing"
