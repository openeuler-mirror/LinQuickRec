#!/bin/bash
apiserver_advertise_address=$1
apiserver_hostname=$2
cluster=$3
etcd_client_port=$4
etcd_node_port=$5
k8s_dir=$6

mkdir -p $k8s_dir/var/lib/etcd
mkdir -p $k8s_dir/var/lib/etcd/wal
mkdir -p $k8s_dir/log/etcd

#  etcd配置
# 如果要用IPv6那么把IPv4地址修改为IPv6即可
cat > $k8s_dir/etc/etcd/etcd.config.yml << EOF 
name: '$apiserver_hostname'
data-dir: $k8s_dir/var/lib/etcd
wal-dir: $k8s_dir/var/lib/etcd/member/wal
snapshot-count: 5000
heartbeat-interval: 100
election-timeout: 1000
listen-peer-urls: 'https://$apiserver_advertise_address:$etcd_node_port'
listen-client-urls: 'https://$apiserver_advertise_address:$etcd_client_port,http://127.0.0.1:$etcd_client_port'
initial-advertise-peer-urls: 'https://$apiserver_advertise_address:$etcd_node_port'
advertise-client-urls: 'https://$apiserver_advertise_address:$etcd_client_port'
initial-cluster: '$cluster'
initial-cluster-token: 'etcd-k8s-cluster'
client-transport-security:
  cert-file: '$k8s_dir/etc/kubernetes/pki/etcd/etcd.pem'
  key-file: '$k8s_dir/etc/kubernetes/pki/etcd/etcd-key.pem'
  client-cert-auth: true
  trusted-ca-file: '$k8s_dir/etc/kubernetes/pki/etcd/etcd-ca.pem'
  auto-tls: true
peer-transport-security:
  cert-file: '$k8s_dir/etc/kubernetes/pki/etcd/etcd.pem'
  key-file: '$k8s_dir/etc/kubernetes/pki/etcd/etcd-key.pem'
  peer-client-cert-auth: true
  trusted-ca-file: '$k8s_dir/etc/kubernetes/pki/etcd/etcd-ca.pem'
  auto-tls: true
debug: false
log-package-levels:
log-outputs: ['stdout', '$k8s_dir/log/etcd/etcd.log','stderr','$k8s_dir/log/etcd/error.log']
force-new-cluster: false
EOF

#  创建etcd.service并启动
cat > /usr/lib/systemd/system/etcd.service << EOF
 
[Unit]
Description=Etcd Service
Documentation=https://coreos.com/etcd/docs/latest/
After=network.target
 
[Service]
Type=notify
ExecStart=/usr/local/bin/etcd --config-file=$k8s_dir/etc/etcd/etcd.config.yml \
          --cipher-suites=TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
Restart=on-failure
RestartSec=10
LimitNOFILE=65536
 
[Install]
WantedBy=multi-user.target
Alias=etcd3.service
 
EOF

mkdir $k8s_dir/etc/kubernetes/pki/etcd
ln -s $k8s_dir/etc/etcd/ssl/* $k8s_dir/etc/kubernetes/pki/etcd/
systemctl daemon-reload
systemctl enable --now etcd
systemctl restart --now etcd
export ETCDCTL_API=3