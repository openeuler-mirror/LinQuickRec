#!/bin/bash
set -e

DATA_DIR="${DATA_DIR:-/var/lib/etcd}"
CLUSTER_SIZE="${CLUSTER_SIZE:-5}"
SERVICE="${SERVICE_NAME:-etcd}"
NS="${CLUSTER_NS:-linquickrec}"

mkdir -p "$DATA_DIR"

echo "etcd ${HOSTNAME}: cluster_size=${CLUSTER_SIZE} data_dir=${DATA_DIR}"

CLUSTER=""
for i in $(seq 0 $((CLUSTER_SIZE - 1))); do
    [ -n "$CLUSTER" ] && CLUSTER+=","
    CLUSTER+="etcd-${i}=http://etcd-${i}.${SERVICE}.${NS}.svc.cluster.local:2380"
done

STATE="new"
[ -d "$DATA_DIR/member/snap" ] && STATE="existing"

echo "  state=${STATE} peers=${CLUSTER_SIZE}"

PEER_URL="http://${HOSTNAME}.${SERVICE}.${NS}.svc.cluster.local"

exec etcd \
  --name "$HOSTNAME" \
  --data-dir "$DATA_DIR" \
  --listen-client-urls http://0.0.0.0:2379 \
  --advertise-client-urls "${PEER_URL}:2379" \
  --listen-peer-urls http://0.0.0.0:2380 \
  --initial-advertise-peer-urls "${PEER_URL}:2380" \
  --initial-cluster-token "linquickrec-etcd" \
  --initial-cluster "$CLUSTER" \
  --initial-cluster-state "$STATE"
