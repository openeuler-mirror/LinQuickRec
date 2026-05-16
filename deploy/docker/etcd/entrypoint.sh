#!/bin/bash
set -e

echo "==========================================="
echo "Starting etcd"
echo "==========================================="

# 数据目录
DATA_DIR="${ETCD_DATA_DIR:-/var/lib/etcd}"
mkdir -p "$DATA_DIR"

# 本机 IP（容器网络下默认用 0.0.0.0）
HOST_IP="${ETCD_HOST_IP:-0.0.0.0}"

# 集群配置
ETCD_NAME="${ETCD_NAME:-etcd-node1}"
ETCD_TOKEN="${ETCD_INITIAL_CLUSTER_TOKEN:-etcd-cluster-1}"

echo "  Host IP: $HOST_IP"
echo "  Data Dir: $DATA_DIR"
echo "  Cluster Name: $ETCD_NAME"

exec etcd \
  --name "$ETCD_NAME" \
  --data-dir "$DATA_DIR" \
  --listen-client-urls http://0.0.0.0:2379 \
  --advertise-client-urls http://${HOST_IP}:2379 \
  --listen-peer-urls http://0.0.0.0:2380 \
  --initial-advertise-peer-urls http://${HOST_IP}:2380 \
  --initial-cluster-token "$ETCD_TOKEN" \
  --initial-cluster ${ETCD_NAME}=http://${HOST_IP}:2380 \
  --initial-cluster-state new
