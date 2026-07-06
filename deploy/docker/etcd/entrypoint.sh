#!/bin/bash
set -e

DATA_DIR="${DATA_DIR:-/var/lib/etcd}"
CLUSTER_SIZE="${CLUSTER_SIZE:-5}"
SERVICE="${SERVICE_NAME:-etcd}"
NS="${CLUSTER_NS:-linquickrec}"

mkdir -p "$DATA_DIR"

STATE="new"
[ -d "$DATA_DIR/member/snap" ] && STATE="existing"

# Resolve own pod DNS to IP for advertise URLs
MY_DNS="${HOSTNAME}.${SERVICE}.${NS}.svc.cluster.local"
MY_IP=$(getent hosts "$MY_DNS" | head -1 | awk '{print $1}')
if [ -z "$MY_IP" ]; then
    echo "WARNING: DNS lookup failed for ${MY_DNS}, using hostname as fallback"
    MY_IP="$MY_DNS"
fi
echo "My IP: ${MY_IP} (${MY_DNS})"

# Build --initial-cluster with IP addresses instead of DNS hostnames
CLUSTER=""
for i in $(seq 0 $((CLUSTER_SIZE - 1))); do
    PEER="etcd-${i}.${SERVICE}.${NS}.svc.cluster.local"
    PEER_IP=$(getent hosts "$PEER" | head -1 | awk '{print $1}')
    if [ -z "$PEER_IP" ]; then
        echo "WARNING: DNS lookup failed for ${PEER}, using hostname as fallback"
        PEER_IP="$PEER"
    fi
    [ -n "$CLUSTER" ] && CLUSTER+=","
    CLUSTER+="etcd-${i}=http://${PEER_IP}:2380"
done

echo "Cluster: ${CLUSTER}"
echo "State: ${STATE}"

exec etcd \
  --name "$HOSTNAME" \
  --data-dir "$DATA_DIR" \
  --listen-client-urls http://0.0.0.0:2379 \
  --advertise-client-urls "http://${MY_IP}:2379" \
  --listen-peer-urls http://0.0.0.0:2380 \
  --initial-advertise-peer-urls "http://${MY_IP}:2380" \
  --initial-cluster-token "linquickrec-etcd" \
  --initial-cluster "$CLUSTER" \
  --initial-cluster-state "$STATE"
