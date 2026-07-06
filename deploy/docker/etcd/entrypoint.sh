#!/bin/bash
set -e

DATA_DIR="${DATA_DIR:-/var/lib/etcd}"
CLUSTER_SIZE="${CLUSTER_SIZE:-5}"
SERVICE="${SERVICE_NAME:-etcd}"
NS="${CLUSTER_NS:-linquickrec}"

mkdir -p "$DATA_DIR"

STATE="new"
[ -d "$DATA_DIR/member/snap" ] && STATE="existing"

CLUSTER=""
for i in $(seq 0 $((CLUSTER_SIZE - 1))); do
    [ -n "$CLUSTER" ] && CLUSTER+=","
    CLUSTER+="etcd-${i}=http://etcd-${i}.${SERVICE}.${NS}.svc.cluster.local:2380"
done

exec etcd \
  --name "$HOSTNAME" \
  --data-dir "$DATA_DIR" \
  --listen-client-urls http://0.0.0.0:2379 \
  --advertise-client-urls "http://${HOSTNAME}.${SERVICE}.${NS}.svc.cluster.local:2379" \
  --listen-peer-urls http://0.0.0.0:2380 \
  --initial-advertise-peer-urls "http://${HOSTNAME}.${SERVICE}.${NS}.svc.cluster.local:2380" \
  --initial-cluster-token "linquickrec-etcd" \
  --initial-cluster "$CLUSTER" \
  --initial-cluster-state "$STATE"
