#!/bin/bash
set -e

DATA_DIR="${DATA_DIR:-/var/lib/etcd}"
CLUSTER_SIZE="${CLUSTER_SIZE:-5}"
SERVICE="${SERVICE_NAME:-etcd}"
NS="${CLUSTER_NS:-linquickrec}"

mkdir -p "$DATA_DIR"

# DNS 预检
NS=$(awk '/^nameserver/ {print $2; exit}' /etc/resolv.conf)

# UDP 53 端口连通性（最多重试 3 次）
if [ -n "$NS" ]; then
    RETRY=0
    while ! timeout 2 bash -c "echo >/dev/udp/${NS}/53" 2>/dev/null; do
        RETRY=$((RETRY + 1))
        if [ $RETRY -ge 3 ]; then
            echo "ERROR: DNS server ${NS}:53 not reachable after 3 attempts, sleeping for manual debug"
            exec sleep infinity
        fi
        echo "DNS server ${NS}:53 not reachable (${RETRY}/3), retrying..."
        sleep 2
    done
    echo "DNS server ${NS}:53 reachable"
fi

# DNS 解析验证（最多重试 3 次，失败后 sleep 等调试）
RETRY=0
while ! timeout 3 nslookup kubernetes.default.svc.cluster.local >/dev/null 2>&1; do
    RETRY=$((RETRY + 1))
    if [ $RETRY -ge 3 ]; then
        echo "ERROR: CoreDNS not resolving after 3 attempts, sleeping for manual debug"
        exec sleep infinity
    fi
    echo "CoreDNS not resolving (${RETRY}/3), retrying..."
    sleep 2
done
echo "CoreDNS is reachable"

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

# Wait for all peers to be reachable before starting etcd
echo "Waiting for all peers to be reachable..."
for i in $(seq 0 $((CLUSTER_SIZE - 1))); do
    PEER="etcd-${i}.${SERVICE}.${NS}.svc.cluster.local"
    while ! ping -W 2 -c 1 "$PEER" >/dev/null 2>&1; do
        echo "  ${PEER} not reachable, retrying..."
        sleep 1
    done
    echo "  ${PEER} reachable"
done

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
