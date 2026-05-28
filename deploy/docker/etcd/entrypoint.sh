#!/bin/bash
set -e

echo "==========================================="
echo "Starting etcd"
echo "==========================================="

DATA_DIR="${ETCD_DATA_DIR:-/var/lib/etcd}"
mkdir -p "$DATA_DIR"

CLUSTER_SIZE="${ETCD_CLUSTER_SIZE:-5}"
SERVICE_NAME="${ETCD_SERVICE_NAME:-etcd}"
NAMESPACE="${ETCD_NAMESPACE:-linquickrec}"

echo "  Hostname: $HOSTNAME"
echo "  Cluster Size: $CLUSTER_SIZE"
echo "  Service: $SERVICE_NAME.$NAMESPACE"

# 生成 initial-cluster 列表
CLUSTER=""
for i in $(seq 0 $((CLUSTER_SIZE - 1))); do
    if [ -n "$CLUSTER" ]; then CLUSTER+=","; fi
    CLUSTER+="etcd-${i}=http://etcd-${i}.${SERVICE_NAME}.${NAMESPACE}.svc.cluster.local:2380"
done

# 有数据目录则视为重启，用 existing 状态
CLUSTER_STATE="new"
if [ -d "$DATA_DIR/member" ]; then
    CLUSTER_STATE="existing"
fi

MY_DNS="${HOSTNAME}.${SERVICE_NAME}.${NAMESPACE}.svc.cluster.local"

echo "  My DNS: $MY_DNS"
echo "  Cluster State: $CLUSTER_STATE"
echo "  Cluster: $CLUSTER"
echo "==========================================="

exec etcd \
  --name "$HOSTNAME" \
  --data-dir "$DATA_DIR" \
  --snapshot-count "${ETCD_SNAPSHOT_COUNT:-5000}" \
  --listen-client-urls http://0.0.0.0:2379 \
  --advertise-client-urls http://${MY_DNS}:2379 \
  --listen-peer-urls http://0.0.0.0:2380 \
  --initial-advertise-peer-urls http://${MY_DNS}:2380 \
  --initial-cluster-token "linquickrec-etcd" \
  --initial-cluster "$CLUSTER" \
  --initial-cluster-state "$CLUSTER_STATE"
