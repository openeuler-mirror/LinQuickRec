#!/bin/bash
set -e

DATA_DIR="${DATA_DIR:-/var/lib/etcd}"
CLUSTER_SIZE="${CLUSTER_SIZE:-5}"
SERVICE="${SERVICE_NAME:-etcd}"
NS="${CLUSTER_NS:-linquickrec}"

mkdir -p "$DATA_DIR"

# DNS 预检
DNS_NS=$(awk '/^nameserver/ {print $2; exit}' /etc/resolv.conf)

# UDP 53 端口连通性（最多重试 3 次）
if [ -n "$DNS_NS" ]; then
    RETRY=0
    while ! timeout 2 bash -c "echo >/dev/udp/${DNS_NS}/53" 2>/dev/null; do
        RETRY=$((RETRY + 1))
        if [ $RETRY -ge 3 ]; then
            echo "ERROR: DNS server ${DNS_NS}:53 not reachable after 3 attempts, sleeping for manual debug"
            exec sleep infinity
        fi
        echo "DNS server ${DNS_NS}:53 not reachable (${RETRY}/3), retrying..."
        sleep 2
    done
    echo "DNS server ${DNS_NS}:53 reachable"
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
echo "Resolving my IP (${MY_DNS})..."
RETRY=0
MY_IP=""
while [ -z "$MY_IP" ]; do
    MY_IP=$(getent hosts "$MY_DNS" 2>/dev/null | head -1 | awk '{print $1}')
    RETRY=$((RETRY + 1))
    if [ -z "$MY_IP" ] && [ $RETRY -ge 30 ]; then
        echo "ERROR: DNS lookup failed for ${MY_DNS} after 30s, sleeping for manual debug"
        exec sleep infinity
    fi
    [ -z "$MY_IP" ] && sleep 1
done
echo "My IP: ${MY_IP} (${MY_DNS})"

# Build --initial-cluster with IP addresses instead of DNS hostnames
CLUSTER=""
for i in $(seq 0 $((CLUSTER_SIZE - 1))); do
    PEER="etcd-${i}.${SERVICE}.${NS}.svc.cluster.local"
    echo "Resolving ${PEER}..."
    RETRY=0
    PEER_IP=""
    while [ -z "$PEER_IP" ]; do
        PEER_IP=$(getent hosts "$PEER" 2>/dev/null | head -1 | awk '{print $1}')
        RETRY=$((RETRY + 1))
        if [ -z "$PEER_IP" ] && [ $RETRY -ge 30 ]; then
            echo "ERROR: DNS lookup failed for ${PEER} after 30s, sleeping for manual debug"
            exec sleep infinity
        fi
        [ -z "$PEER_IP" ] && sleep 1
    done
    echo "  ${PEER} -> ${PEER_IP}"
    [ -n "$CLUSTER" ] && CLUSTER+=","
    CLUSTER+="etcd-${i}=http://${PEER_IP}:2380"
done

echo "Cluster: ${CLUSTER}"
echo "State: ${STATE}"

echo "All peers DNS-resolved, starting etcd (peer discovery handled by etcd)"

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
