#!/bin/bash

# 数据目录
DATA_DIR="/var/lib/etcd" # 可按需更改

# 本机IP
HOST_IP="127.0.0.1"

# 创建数据目录
mkdir -p $DATA_DIR

nohup etcd \
  --name etcd-node1 \
  --data-dir $DATA_DIR \
  --listen-client-urls http://0.0.0.0:2379 \
  --advertise-client-urls http://$HOST_IP:2379 \
  --listen-peer-urls http://0.0.0.0:2380 \
  --initial-advertise-peer-urls http://$HOST_IP:2380 \
  --initial-cluster-token etcd-cluster-1 \
  --initial-cluster etcd-node1=http://$HOST_IP:2380 \
  --initial-cluster-state new \
  2>&1 > ${DATA_DIR}/etcd.log &