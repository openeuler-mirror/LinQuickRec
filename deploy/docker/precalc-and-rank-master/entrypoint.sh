#!/bin/bash
set -e

REG="${REGISTRY_BACKEND:-discovery_server}"
ETCD="${ETCD_ENDPOINTS:-etcd:2379}"
DISC="${DISCOVERY_ADDR:-discovery-server:8100}"

echo "=== Precalc-and-Rank-Master ==="
echo "Precalc: ${PRECALC_PORT:-8003}  RankMaster: ${RANK_MASTER_PORT:-8004}  Registry: $REG"

DISC_FLAGS="--registry_backend=$REG"
[ "$REG" = "etcd" ] && DISC_FLAGS="--registry_backend=etcd --etcd_endpoints=$ETCD" || DISC_FLAGS="--discovery_addr=$DISC"

/app/build/bin/precalc_and_rank_master \
    --server_port=${PRECALC_PORT:-8003} \
    --rank_master_server_port=${RANK_MASTER_PORT:-8004} \
    --registry_backend="$REG" \
    --etcd_endpoints="$ETCD" \
    --discovery_addr="$DISC" &
PID=$!
sleep 2

/app/discovery_client --service_type=precalc_service --service_port=${PRECALC_PORT:-8003} $DISC_FLAGS --host=auto --heartbeat_interval=5 &
/app/discovery_client --service_type=rank_service --service_port=${RANK_MASTER_PORT:-8004} $DISC_FLAGS --host=auto --heartbeat_interval=5 &

wait $PID
