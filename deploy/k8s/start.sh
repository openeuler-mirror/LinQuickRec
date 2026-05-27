#!/bin/bash

function parseAddress(){
    address=${1#*@}
    ipv4=${address%[*}
    if [ "$ipv6_enabled" = "true" ] || [ "$ipv6_enabled" = "TRUE" ];then
        ipv6=`echo $address | cut -d '[' -f2| cut -d ']' -f1`
        echo $ipv4,$ipv6
    else
        echo $ipv4
    fi
}

function getNodeIP() {
    address=${1#*@}
    echo ${address%[*}
}

function getClusterIPv4() {
    service=${1%/*}
    last=${service##*.}
    let last+=1
    echo ${service%.*}.$last
}

function getClusterIPv6() {
    service=${1%/*}
    last=${service##*:}
    if [ "$last"x = "0x" ];then
        last=1
    else
        let last+=1
    fi
    echo ${service%:*}:$last
}

function getETCDCluster() {
    address=${1#*@}
    hostname=${1%@*}
    etcd_port=$2
    etcd_ipv4=${address%[*}
    etcd_instance=$hostname=https://$etcd_ipv4:$etcd_port
    echo $etcd_instance
}

function getAPICluster() {
    address=${1#*@}
    etcd_port=$2
    etcd_ipv4=${address%[*}
    etcd_instance=https://$etcd_ipv4:$etcd_port
    echo $etcd_instance
}

set -e

k8s_env_path=$1

if [ $k8s_env_path ]; then
    if [ ! -e $k8s_env_path ]; then
        echo "找不到配置文件"
        exit 0
    else
        while read line
        do
            if [[ ! $line =~ ^# && ! $line =~ ^$ ]];then
                export $line
            fi
        done < $k8s_env_path
        echo "k8s环境变量导入成功"
    fi
fi

if [ ! $apiserver_address ]; then
    apiserver_address=$(ifconfig enp39s0 | grep 'inet ' | cut -d: -f2 | awk '{ print $2}')
   # echo "缺少重要参数apiserver_address"
   # exit 1
    
fi

# 补全配置参数
ipv6_enabled=${ipv6_enabled:-false}
apiserver_bind_port=${apiserver_bind_port:-6443}
etcd_client_port=${etcd_client_port:-2379}
etcd_node_port=${etcd_node_port:-2380}
if [ "$ipv6_enabled" = "true" ] || [ "$ipv6_enabled" = "TRUE" ];then
    pod_network_cidr=${pod_network_cidr:-172.16.0.0/12,fc00::/48}
    service_cluster_ip=${service_cluster_ip:-10.96.0.0/12,fd00::/108}
else
    pod_network_cidr=${pod_network_cidr:-172.16.0.0/12}
    service_cluster_ip=${service_cluster_ip:-10.96.0.0/12}
fi
service_dns_domain=${service_dns_domain:-10.96.0.20}
containerd_root=${containerd_root:-/var/lib/containerd}
use_local=${use_local:-false}
k8s_version=${k8s_version:--1}
etcd_version=${etcd_version:-1.1.0.102}
coredns_version=${coredns_version:-1.1.0.102}
local_path=${local_path:-/root/kubernetes/_output/bin}
k8s_dir=${k8s_dir:-/root/kubernetes}
log_level=${log_level:-2}
architecture=`uname -m`
sandbox=docker.io/library/pause-$architecture:3.8
e2e=${e2e:-false}

#  安装k8s二进制组件
if [ $use_local = true ] || [ $use_local = TRUE ]; then
    bin_dir=$local_path
else
    if [ $k8s_version = -1 ];then
        # 获取CaaSCore最新k8s二进制包版本号，生成下载链接
        version=$(curl -s https://cmc.cloudartifact.szv.dragon.tools.huawei.com/artifactory/sz-software-release/cloudbasedsoftware1664351025426/release/CaaSCore/ -k | grep "a href" | tail -1)
        version=${version%\/*}
        version=${version#*\"}
        k8s_url=https://cmc.cloudartifact.szv.dragon.tools.huawei.com/artifactory/sz-software-release/cloudbasedsoftware1664351025426/release/CaaSCore/$version/kubernetes-$version-$architecture.tar.gz
    else
        # 指定k8s二进制包版本号，生成下载链接
        k8s_url=https://cmc.cloudartifact.szv.dragon.tools.huawei.com/artifactory/sz-software-release/cloudbasedsoftware1664351025426/release/CaaSCore/$k8s_version/kubernetes-$k8s_version-$architecture.tar.gz
    fi
    mkdir -p $k8s_dir/k8s-download/
    #wget -nc --no-check-certificate $k8s_url -O $k8s_dir/k8s-download/kubernetes.tar.gz >/dev/null 2>&1
    cp $package_path/kubernetes-v1.19.9-CaaSCore1.1.0.103-aarch64.tar.gz /root/kubernetes/k8s-download/kubernetes.tar.gz
    if [ ! -s $k8s_dir/k8s-download/kubernetes.tar.gz ];then
        echo "下载k8s二进制包失败"
        rm -rf $k8s_dir
        exit 1
    fi
    tar -xf $k8s_dir/k8s-download/kubernetes.tar.gz  --strip-components=1 -C $k8s_dir/k8s-download/
    chmod +x $k8s_dir/k8s-download/*
    bin_dir=$k8s_dir/k8s-download/kubernetes
fi

#  获取脚本所在文件夹路径
basepath=$(cd `dirname -- $0`; pwd)
mkdir -p $k8s_dir/k8s-install

#  拼接所有节点ip字符串
ip_list=$(parseAddress $apiserver_address)

OLD_IFS="$IFS"
if [ $masters_address ]; then
  IFS=","
  masters=($masters_address)
  IFS="${OLD_IFS}"
  for master in ${masters[@]}
      do
          ip_list=$ip_list,$(parseAddress $master)
      done
fi

if [ $nodes_address ]; then
  IFS=","
  nodes=($nodes_address)
  IFS="${OLD_IFS}"
  for node in ${nodes[@]}
      do
          ip_list=$ip_list,$(parseAddress $node)
      done
fi

# 本机生成卸载脚本
filePath="$k8s_dir/k8s-install/uninstall.sh"
if [ -e "$filePath" ]; then
    rm -rf $filePath;
fi
cat > $filePath << EOF;
#!/bin/bash
echo "start to uninstall kubernetes"
if [ -f "/usr/local/bin/kubectl" ];then
  for resource in deployment daemonset pod
      do
          timeout 60s kubectl delete \$resource --all -A
      done
fi
systemctl stop kube-proxy kubelet kube-apiserver kube-controller-manager kube-scheduler etcd >/dev/null 2>&1

# 卸载/run/containerd挂载，仅删除overlay与shm
mount -l | grep /run/containerd/io.containerd | egrep 'overlay|shm' | awk '{print \$3}' | xargs umount -l >/dev/null 2>&1
# 卸载/var/lib/containerd挂载，仅删除overlay，防止卸载磁盘挂载
mount -l | grep /var/lib/containerd/io.containerd | grep 'overlay' | awk '{print \$3}' | xargs umount -l >/dev/null 2>&1
# 卸载/var/lib/kubelet挂载
mount -l | grep /var/lib/kubelet/pods | awk '{print \$3}' | xargs umount -l >/dev/null 2>&1
rm -rf /var/lib/kubelet
rm -rf /run/containerd
rm -rf /var/lib/containerd
rm -rf /var/log/pods
rm -rf /var/log/containers
rm -rf $k8s_dir
rm -rf /root/.kube
rm -rf /etc/cni/net.d/10-calico.conflist
rm -rf /etc/cni/net.d/10-containerd-net.conflist
rm -rf /etc/cni/net.d/calico-kubeconfig
rm -rf $containerd_root
rm -rf /usr/lib/systemd/system/kube{-apiserver,let,ctl,-controller-manager,-scheduler,-proxy}.service /usr/lib/systemd/system/etcd.service
rm -rf /usr/local/bin/kube{-apiserver,let,ctl,-controller-manager,-scheduler,-proxy,-log-runner} /usr/local/bin/etcd* /usr/local/bin/cfssl*
systemctl daemon-reload
echo "uninstall kubernetes finished"
EOF

# 发送卸载脚本到其他node(如果存在)
if [ $nodes_address ]; then
    IFS=","
    nodes=($nodes_address)
    IFS="${OLD_IFS}"
    for node in ${nodes[@]}
        do
            ssh $(getNodeIP $node) "if [ -e "$filePath" ];then rm -rf $filePath;fi;mkdir -p $k8s_dir/k8s-install;"
            scp $filePath $(getNodeIP $node):$k8s_dir/k8s-install/
        done
fi

#  在本机执行pre.sh
echo "环境前置配置"
sh $basepath/pre.sh ${apiserver_address%@*} $k8s_dir $sandbox $containerd_root $architecture $e2e $package_path
[ -f /etc/resolv.conf ] || touch /etc/resolv.conf

#  对其他master执行pre.sh（如果存在）
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            ssh $(getNodeIP $master) "mkdir -p $k8s_dir/k8s-install/ $k8s_dir/images/ $package_path/"
            scp $basepath/pre.sh $(getNodeIP $master):$k8s_dir/k8s-install/
            scp $package_path/cri-containerd-cni-1.7.5-linux-arm64.tar.gz $(getNodeIP $master):$package_path
            ssh $(getNodeIP $master) "sh $k8s_dir/k8s-install/pre.sh ${master%@*} $k8s_dir $sandbox $containerd_root $architecture $e2e $package_path"
	    ssh $(getNodeIP $master) "[ -f /etc/resolv.conf ] || touch /etc/resolv.conf"
            echo "ssh $(getNodeIP $master) sh $filePath" >> $filePath
        done
fi

#  对所有node执行pre.sh（如果存在）
if [ $nodes_address ]; then
    IFS=","
    nodes=($nodes_address)
    IFS="${OLD_IFS}"
    for node in ${nodes[@]}
        do
            scp $basepath/pre.sh $(getNodeIP $node):$k8s_dir/k8s-install/
            ssh $(getNodeIP $node) "mkdir -p $k8s_dir/images/ && sh ${k8s_dir}/k8s-install/pre.sh ${node%@*} $k8s_dir $sandbox $containerd_root $architecture $e2e $package_path"
            ssh $(getNodeIP $node) "mkdir -p $package_path"
            scp $package_path/cri-containerd-cni-1.7.5-linux-arm64.tar.gz $(getNodeIP $node):$package_path
	    ssh $(getNodeIP $node) "[ -f /etc/resolv.conf ] || touch /etc/resolv.conf"
            echo "ssh $(getNodeIP $node) sh $filePath" >> $filePath
        done
fi

# 发送卸载脚本到其他master(如果存在)
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            ssh $(getNodeIP $master) "if [ -e "$filePath" ];then rm -rf $filePath;fi;"
            scp $filePath $(getNodeIP $master):$k8s_dir/k8s-install/
            ssh $(getNodeIP $master) "echo \"ssh $(getNodeIP $apiserver_address) \"sh $filePath\"\" >> $filePath;
            sed -i '/ssh $(getNodeIP $master)/d' $filePath"
        done
fi

#  下载etcd
echo "下载etcd二进制文件"
# wget -nc --no-check-certificate https://cmc.cloudartifact.szv.dragon.tools.huawei.com/artifactory/sz-software-release/cloudbasedsoftware1664351025426/release/CaaSCore-etcd/$etcd_version/etcd-v3.5.4-$etcd_version-$architecture.tar.gz -O $k8s_dir/k8s-download/etcd.tar.gz >/dev/null 2>&1
cp $package_path/etcd-v3.5.4-1.1.0.102-aarch64.tar.gz /root/kubernetes/k8s-download/etcd.tar.gz
tar -xf $k8s_dir/k8s-download/etcd.tar.gz -C $k8s_dir/k8s-download/
mv $k8s_dir/k8s-download/etcd-*/etcd /usr/local/bin/
mv $k8s_dir/k8s-download/etcd-*/etcdctl /usr/local/bin/
rm -rf $k8s_dir/k8s-download/etcd-*
cp $bin_dir/kube{let,ctl,-apiserver,-controller-manager,-scheduler,-proxy,-log-runner} /usr/local/bin/
mkdir -p $k8s_dir/images/
cp $bin_dir/pause* $k8s_dir/images/

#  发送组件到其他master（如果存在）
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            scp /usr/local/bin/kube{let,ctl,-apiserver,-controller-manager,-scheduler,-proxy,-log-runner} $(getNodeIP $master):/usr/local/bin/
            scp /usr/local/bin/etcd* $(getNodeIP $master):/usr/local/bin/
            scp $k8s_dir/images/pause* $(getNodeIP $master):$k8s_dir/images/
        done
fi

#  发送kubelet和kube-proxy到所有node(如果存在)
if [ $nodes_address ]; then
    IFS=","
    nodes=($nodes_address)
    IFS="${OLD_IFS}"
    for node in ${nodes[@]}
        do
            scp /usr/local/bin/kube{let,-proxy,-log-runner} $(getNodeIP $node):/usr/local/bin/
            scp $k8s_dir/images/pause* $(getNodeIP $node):$k8s_dir/images/
        done
fi

mkdir -p $k8s_dir/pki
cat > $k8s_dir/pki/admin-csr.json << EOF 
{
  "CN": "admin",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "ST": "Beijing",
      "L": "Beijing",
      "O": "system:masters",
      "OU": "Kubernetes-manual"
    }
  ]
}
EOF

cat > $k8s_dir/pki/ca-config.json << EOF 
{
  "signing": {
    "default": {
      "expiry": "876000h"
    },
    "profiles": {
      "kubernetes": {
        "usages": [
            "signing",
            "key encipherment",
            "server auth",
            "client auth"
        ],
        "expiry": "876000h"
      }
    }
  }
}
EOF

cat > $k8s_dir/pki/etcd-ca-csr.json  << EOF 
{
  "CN": "etcd",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "ST": "Beijing",
      "L": "Beijing",
      "O": "etcd",
      "OU": "Etcd Security"
    }
  ],
  "ca": {
    "expiry": "876000h"
  }
}
EOF

cat > $k8s_dir/pki/front-proxy-ca-csr.json  << EOF 
{
  "CN": "kubernetes",
  "key": {
     "algo": "rsa",
     "size": 2048
  },
  "ca": {
    "expiry": "876000h"
  }
}
EOF

cat > $k8s_dir/pki/kubelet-csr.json  << EOF 
{
  "CN": "system:node:\$NODE",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "L": "Beijing",
      "ST": "Beijing",
      "O": "system:nodes",
      "OU": "Kubernetes-manual"
    }
  ]
}
EOF

cat > $k8s_dir/pki/manager-csr.json << EOF 
{
  "CN": "system:kube-controller-manager",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "ST": "Beijing",
      "L": "Beijing",
      "O": "system:kube-controller-manager",
      "OU": "Kubernetes-manual"
    }
  ]
}
EOF

cat > $k8s_dir/pki/apiserver-csr.json << EOF 
{
  "CN": "kube-apiserver",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "ST": "Beijing",
      "L": "Beijing",
      "O": "Kubernetes",
      "OU": "Kubernetes-manual"
    }
  ]
}
EOF

cat > $k8s_dir/pki/ca-csr.json   << EOF 
{
  "CN": "kubernetes",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "ST": "Beijing",
      "L": "Beijing",
      "O": "Kubernetes",
      "OU": "Kubernetes-manual"
    }
  ],
  "ca": {
    "expiry": "876000h"
  }
}
EOF

cat > $k8s_dir/pki/etcd-csr.json << EOF 
{
  "CN": "etcd",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "ST": "Beijing",
      "L": "Beijing",
      "O": "etcd",
      "OU": "Etcd Security"
    }
  ]
}
EOF

cat > $k8s_dir/pki/front-proxy-client-csr.json  << EOF 
{
  "CN": "front-proxy-client",
  "key": {
     "algo": "rsa",
     "size": 2048
  }
}
EOF

cat > $k8s_dir/pki/kube-proxy-csr.json  << EOF 
{
  "CN": "system:kube-proxy",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "ST": "Beijing",
      "L": "Beijing",
      "O": "system:kube-proxy",
      "OU": "Kubernetes-manual"
    }
  ]
}
EOF

cat > $k8s_dir/pki/scheduler-csr.json << EOF 
{
  "CN": "system:kube-scheduler",
  "key": {
    "algo": "rsa",
    "size": 2048
  },
  "names": [
    {
      "C": "CN",
      "ST": "Beijing",
      "L": "Beijing",
      "O": "system:kube-scheduler",
      "OU": "Kubernetes-manual"
    }
  ]
}
EOF

#  下载证书生成工具
# wget -nc --no-check-certificate https://cmc-szver-artifactory.cmc.tools.huawei.com/artifactory/cmc-software-release/CloudSOP/caascore/1.0.0/tools/$architecture/cfssl-1.6.1-linux-$architecture.zip -O $k8s_dir/k8s-download/cfssl.zip >/dev/null 2>&1
cp $package_path/cfssl-1.6.1-linux-aarch64.zip /root/kubernetes/k8s-download/cfssl.zip
unzip $k8s_dir/k8s-download/cfssl.zip -d $k8s_dir/k8s-download/
mv $k8s_dir/k8s-download/cfssl /usr/local/bin/cfssl
mv $k8s_dir/k8s-download/cfssljson /usr/local/bin/cfssljson
rm -rf $k8s_dir/k8s-download/cfssl*
chmod +x /usr/local/bin/cfssl /usr/local/bin/cfssljson

#  创建etcd证书存放目录
mkdir -p $k8s_dir/etc/etcd/ssl
cd $k8s_dir/pki

#  拼接master字符串
masters_info=${apiserver_address%@*},$(parseAddress $apiserver_address)

if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            masters_info=$masters_info,${master%@*},$(parseAddress $master)
        done
fi

#  生成etcd证书和etcd证书的key
cfssl gencert -initca etcd-ca-csr.json | cfssljson -bare $k8s_dir/etc/etcd/ssl/etcd-ca
cfssl gencert \
   -ca=$k8s_dir/etc/etcd/ssl/etcd-ca.pem \
   -ca-key=$k8s_dir/etc/etcd/ssl/etcd-ca-key.pem \
   -config=ca-config.json \
   -hostname=127.0.0.1,$masters_info \
   -profile=kubernetes \
   etcd-csr.json | cfssljson -bare $k8s_dir/etc/etcd/ssl/etcd

#  将证书复制到其他master（如果存在）
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            ssh $(getNodeIP $master) "mkdir -p ${k8s_dir}/etc/etcd/ssl"
            for FILE in etcd-ca-key.pem  etcd-ca.pem  etcd-key.pem  etcd.pem
                do
                    scp $k8s_dir/etc/etcd/ssl/$FILE $(getNodeIP $master):$k8s_dir/etc/etcd/ssl/$FILE
                done
        done
fi

#  service网段第一个可用ip
if [ "$ipv6_enabled" = "true" ] || [ "$ipv6_enabled" = "TRUE" ];then
    servicev4=${service_cluster_ip%,*}
    servicev6=${service_cluster_ip#*,}
    new_service=$(getClusterIPv4 $servicev4),$(getClusterIPv6 $servicev6)
else
    new_service=$(getClusterIPv4 $service_cluster_ip)
fi

#  创建k8s证书存放目录
mkdir -p $k8s_dir/etc/kubernetes/pki
cfssl gencert -initca ca-csr.json | cfssljson -bare $k8s_dir/etc/kubernetes/pki/ca
#  生成一个根证书
#  service-cidr是k8s service的网段
#  如果不是高可用集群，apiserver-advertise-address为Master的IP
#  若没有IPv6 可删除可保留 
cfssl gencert   \
   -ca=$k8s_dir/etc/kubernetes/pki/ca.pem   \
   -ca-key=$k8s_dir/etc/kubernetes/pki/ca-key.pem   \
   -config=ca-config.json   \
   -hostname=$new_service,$ip_list,127.0.0.1,kubernetes,kubernetes.default,kubernetes.default.svc,kubernetes.default.svc.cluster,kubernetes.default.svc.cluster.local  \
   -profile=kubernetes   apiserver-csr.json | cfssljson -bare $k8s_dir/etc/kubernetes/pki/apiserver

#  生成kube-apiserver聚合证书
cfssl gencert   -initca front-proxy-ca-csr.json | cfssljson -bare $k8s_dir/etc/kubernetes/pki/front-proxy-ca 
cfssl gencert  \
   -ca=$k8s_dir/etc/kubernetes/pki/front-proxy-ca.pem   \
   -ca-key=$k8s_dir/etc/kubernetes/pki/front-proxy-ca-key.pem   \
   -config=ca-config.json   \
   -profile=kubernetes   front-proxy-client-csr.json | cfssljson -bare $k8s_dir/etc/kubernetes/pki/front-proxy-client

#  生成kube-controller-manager的证书
cfssl gencert \
   -ca=$k8s_dir/etc/kubernetes/pki/ca.pem \
   -ca-key=$k8s_dir/etc/kubernetes/pki/ca-key.pem \
   -config=ca-config.json \
   -profile=kubernetes \
   manager-csr.json | cfssljson -bare $k8s_dir/etc/kubernetes/pki/controller-manager

#  设置一个集群项
kubectl config set-cluster kubernetes \
     --certificate-authority=$k8s_dir/etc/kubernetes/pki/ca.pem \
     --embed-certs=true \
     --server=https://$(getNodeIP $apiserver_address):$apiserver_bind_port \
     --kubeconfig=$k8s_dir/etc/kubernetes/controller-manager.kubeconfig

#  设置一个环境项，一个上下文
kubectl config set-context system:kube-controller-manager@kubernetes \
    --cluster=kubernetes \
    --user=system:kube-controller-manager \
    --kubeconfig=$k8s_dir/etc/kubernetes/controller-manager.kubeconfig
 
#  设置一个用户项
kubectl config set-credentials system:kube-controller-manager \
     --client-certificate=$k8s_dir/etc/kubernetes/pki/controller-manager.pem \
     --client-key=$k8s_dir/etc/kubernetes/pki/controller-manager-key.pem \
     --embed-certs=true \
     --kubeconfig=$k8s_dir/etc/kubernetes/controller-manager.kubeconfig
 
#  设置默认环境
kubectl config use-context system:kube-controller-manager@kubernetes \
     --kubeconfig=$k8s_dir/etc/kubernetes/controller-manager.kubeconfig

cfssl gencert \
   -ca=$k8s_dir/etc/kubernetes/pki/ca.pem \
   -ca-key=$k8s_dir/etc/kubernetes/pki/ca-key.pem \
   -config=ca-config.json \
   -profile=kubernetes \
   admin-csr.json | cfssljson -bare $k8s_dir/etc/kubernetes/pki/admin

#  设置一个集群项 
kubectl config set-cluster kubernetes     \
   --certificate-authority=$k8s_dir/etc/kubernetes/pki/ca.pem     \
   --embed-certs=true     \
   --server=https://$(getNodeIP $apiserver_address):$apiserver_bind_port     \
   --kubeconfig=$k8s_dir/etc/kubernetes/admin.kubeconfig

#  设置一个用户项 
kubectl config set-credentials kubernetes-admin  \
   --client-certificate=$k8s_dir/etc/kubernetes/pki/admin.pem     \
   --client-key=$k8s_dir/etc/kubernetes/pki/admin-key.pem     \
   --embed-certs=true     \
   --kubeconfig=$k8s_dir/etc/kubernetes/admin.kubeconfig
 
#  设置一个环境项，一个上下文
kubectl config set-context kubernetes-admin@kubernetes    \
   --cluster=kubernetes     \
   --user=kubernetes-admin     \
   --kubeconfig=$k8s_dir/etc/kubernetes/admin.kubeconfig
 
#  设置默认环境
kubectl config use-context kubernetes-admin@kubernetes  --kubeconfig=$k8s_dir/etc/kubernetes/admin.kubeconfig

#  生成kube-scheduler证书
 cfssl gencert \
   -ca=$k8s_dir/etc/kubernetes/pki/ca.pem \
   -ca-key=$k8s_dir/etc/kubernetes/pki/ca-key.pem \
   -config=ca-config.json \
   -profile=kubernetes \
   scheduler-csr.json | cfssljson -bare $k8s_dir/etc/kubernetes/pki/scheduler
 
#  设置一个集群项
kubectl config set-cluster kubernetes \
   --certificate-authority=$k8s_dir/etc/kubernetes/pki/ca.pem \
   --embed-certs=true \
   --server=https://$(getNodeIP $apiserver_address):$apiserver_bind_port \
   --kubeconfig=$k8s_dir/etc/kubernetes/scheduler.kubeconfig

#  设置一个用户项
kubectl config set-credentials system:kube-scheduler \
   --client-certificate=$k8s_dir/etc/kubernetes/pki/scheduler.pem \
   --client-key=$k8s_dir/etc/kubernetes/pki/scheduler-key.pem \
   --embed-certs=true \
   --kubeconfig=$k8s_dir/etc/kubernetes/scheduler.kubeconfig
 
#  设置一个环境项，一个上下文
kubectl config set-context system:kube-scheduler@kubernetes \
   --cluster=kubernetes \
   --user=system:kube-scheduler \
   --kubeconfig=$k8s_dir/etc/kubernetes/scheduler.kubeconfig
 
#  设置默认环境
kubectl config use-context system:kube-scheduler@kubernetes \
   --kubeconfig=$k8s_dir/etc/kubernetes/scheduler.kubeconfig

#  生成kube-proxy证书
cfssl gencert \
   -ca=$k8s_dir/etc/kubernetes/pki/ca.pem \
   -ca-key=$k8s_dir/etc/kubernetes/pki/ca-key.pem \
   -config=ca-config.json \
   -profile=kubernetes \
   kube-proxy-csr.json | cfssljson -bare $k8s_dir/etc/kubernetes/pki/kube-proxy

#  设置一个集群项 
kubectl config set-cluster kubernetes     \
   --certificate-authority=$k8s_dir/etc/kubernetes/pki/ca.pem     \
   --embed-certs=true     \
   --server=https://$(getNodeIP $apiserver_address):$apiserver_bind_port     \
   --kubeconfig=$k8s_dir/etc/kubernetes/kube-proxy.kubeconfig
 
#  设置一个用户项
kubectl config set-credentials kube-proxy  \
   --client-certificate=$k8s_dir/etc/kubernetes/pki/kube-proxy.pem     \
   --client-key=$k8s_dir/etc/kubernetes/pki/kube-proxy-key.pem     \
   --embed-certs=true     \
   --kubeconfig=$k8s_dir/etc/kubernetes/kube-proxy.kubeconfig

# 设置一个环境项，一个上下文 
kubectl config set-context kube-proxy@kubernetes    \
   --cluster=kubernetes     \
   --user=kube-proxy     \
   --kubeconfig=$k8s_dir/etc/kubernetes/kube-proxy.kubeconfig
 
#  设置默认环境
kubectl config use-context kube-proxy@kubernetes  --kubeconfig=$k8s_dir/etc/kubernetes/kube-proxy.kubeconfig

#  创建ServiceAccount Key --secret
openssl genrsa -out $k8s_dir/etc/kubernetes/pki/sa.key 2048
openssl rsa -in $k8s_dir/etc/kubernetes/pki/sa.key -pubout -out $k8s_dir/etc/kubernetes/pki/sa.pub

#  复制证书到其他master(如果存在)
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            ssh $(getNodeIP $master) "mkdir -p ${k8s_dir}/etc/kubernetes/pki/"
            for FILE in $(ls ${k8s_dir}/etc/kubernetes/pki | grep -v etcd)
                do
                    scp $k8s_dir/etc/kubernetes/pki/$FILE $(getNodeIP $master):$k8s_dir/etc/kubernetes/pki/$FILE
                done
            for FILE in admin.kubeconfig controller-manager.kubeconfig scheduler.kubeconfig
                do
                    scp $k8s_dir/etc/kubernetes/$FILE $(getNodeIP $master):$k8s_dir/etc/kubernetes/$FILE
                done
        done
fi

#  拼接cluster字符串
cluster=$(getETCDCluster $apiserver_address $etcd_node_port)
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            cluster=$cluster,$(getETCDCluster $master $etcd_node_port)
        done
fi

#  本机执行etcd_deployment.sh
sh $basepath/etcd_deployment.sh $(getNodeIP $apiserver_address) ${apiserver_address%@*} $cluster $etcd_client_port $etcd_node_port $k8s_dir

#  其他master执行etcd_deployment.sh(如果存在)
if [ $masters_address ]; then
    for master in ${masters[@]}
        do
            scp $basepath/etcd_deployment.sh $(getNodeIP $master):$k8s_dir/k8s-install/
            ssh $(getNodeIP $master) "sh $k8s_dir/k8s-install/etcd_deployment.sh $(getNodeIP $master) ${master%@*} $cluster $etcd_client_port $etcd_node_port $k8s_dir"
        done
fi

#  拼接apiserver_cluster字符串
apiserver_cluster=$(getAPICluster $apiserver_address $etcd_client_port)

if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            apiserver_cluster=$apiserver_cluster,$(getAPICluster $master $etcd_client_port)
        done
fi

#  本机执行apiserver_deployment.sh
sh $basepath/apiserver_deployment.sh $(getNodeIP $apiserver_address) $apiserver_cluster $apiserver_bind_port $service_cluster_ip $k8s_dir $log_level $e2e
#  其他master执行apiserver_deployment.sh(如果存在)
if [ $masters_address ]; then
    for master in ${masters[@]}
        do
            scp $basepath/apiserver_deployment.sh $(getNodeIP $master):$k8s_dir/k8s-install/
            ssh $(getNodeIP $master) "sh ${k8s_dir}/k8s-install/apiserver_deployment.sh $(getNodeIP $master) $apiserver_cluster $apiserver_bind_port $service_cluster_ip $k8s_dir $log_level $e2e"
        done
fi

#  本机执行controller_manager_deployment.sh
sh $basepath/controller_manager_deployment.sh $service_cluster_ip $pod_network_cidr $k8s_dir $log_level $e2e

#  其他master执行controller_manager_deployment.sh（如果存在）
if [ $masters_address ]; then
    for master in ${masters[@]}
        do
            scp $basepath/controller_manager_deployment.sh $(getNodeIP $master):$k8s_dir/k8s-install/
            ssh $(getNodeIP $master) "sh ${k8s_dir}/k8s-install/controller_manager_deployment.sh $service_cluster_ip $pod_network_cidr $k8s_dir $log_level $e2e"
        done
fi

#  本机执行scheduler_deployment.sh
sh $basepath/scheduler_deployment.sh $k8s_dir $log_level

#  其他master执行scheduler_deployment.sh（如果存在）
if [ $masters_address ]; then
    for master in ${masters[@]}
        do
            scp $basepath/scheduler_deployment.sh $(getNodeIP $master):$k8s_dir/k8s-install/
            ssh $(getNodeIP $master) "sh ${k8s_dir}/k8s-install/scheduler_deployment.sh $k8s_dir $log_level"
        done
fi

mkdir -p $k8s_dir/bootstrap

#  设置一个集群项
kubectl config set-cluster kubernetes     \
   --certificate-authority=$k8s_dir/etc/kubernetes/pki/ca.pem     \
   --embed-certs=true     --server=https://$(getNodeIP $apiserver_address):$apiserver_bind_port     \
   --kubeconfig=$k8s_dir/etc/kubernetes/bootstrap-kubelet.kubeconfig
 
#  设置一个用户项
kubectl config set-credentials tls-bootstrap-token-user     \
   --token=c8ad9c.2e4d610cf3e7426e \
   --kubeconfig=$k8s_dir/etc/kubernetes/bootstrap-kubelet.kubeconfig
 
#  设置一个环境项，一个上下文
kubectl config set-context tls-bootstrap-token-user@kubernetes     \
   --cluster=kubernetes     \
   --user=tls-bootstrap-token-user     \
   --kubeconfig=$k8s_dir/etc/kubernetes/bootstrap-kubelet.kubeconfig

#  设置默认环境
kubectl config use-context tls-bootstrap-token-user@kubernetes     \
   --kubeconfig=$k8s_dir/etc/kubernetes/bootstrap-kubelet.kubeconfig
#  token的位置在bootstrap.secret.yaml，如果修改的话到这个文件修改

rm -rf /root/.kube
mkdir /root/.kube/
cp $k8s_dir/etc/kubernetes/admin.kubeconfig /root/.kube/
mv /root/.kube/admin.kubeconfig /root/.kube/config

cd $k8s_dir/etc/kubernetes/

#  发送到其他节点(如果存在)
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            ssh $(getNodeIP $master) mkdir -p $k8s_dir/etc/kubernetes/pki
            for FILE in pki/ca.pem pki/ca-key.pem pki/front-proxy-ca.pem bootstrap-kubelet.kubeconfig kube-proxy.kubeconfig
                do
                    scp $k8s_dir/etc/kubernetes/$FILE $(getNodeIP $master):$k8s_dir/etc/kubernetes/$FILE
                done
        done
fi

if [ $nodes_address ]; then
    IFS=","
    nodes=($nodes_address)
    IFS="${OLD_IFS}"
    for node in ${nodes[@]}
        do
            ssh $(getNodeIP $node) mkdir -p $k8s_dir/etc/kubernetes/pki
            for FILE in pki/ca.pem pki/ca-key.pem pki/front-proxy-ca.pem bootstrap-kubelet.kubeconfig kube-proxy.kubeconfig
                do
                    scp $k8s_dir/etc/kubernetes/$FILE $(getNodeIP $node):$k8s_dir/etc/kubernetes/$FILE
                done
        done
fi

#  本机执行node_deployment.sh
sh $basepath/node_deployment.sh $(parseAddress $apiserver_address) $service_dns_domain $pod_network_cidr $k8s_dir $log_level $e2e

#  其他节点执行node_deployment.sh（如果存在）
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            scp ${basepath}/node_deployment.sh $(getNodeIP $master):$k8s_dir/k8s-install/
            ssh $(getNodeIP $master) "sh $k8s_dir/k8s-install/node_deployment.sh $(parseAddress $master) $service_dns_domain $pod_network_cidr $k8s_dir $log_level $e2e && rm -rf /root/.kube && mkdir -p /root/.kube/ $k8s_dir/images/calico/ && cp $k8s_dir/etc/kubernetes/admin.kubeconfig /root/.kube/ && mv /root/.kube/admin.kubeconfig /root/.kube/config"
        done
fi

if [ $nodes_address ]; then
    IFS=","
    nodes=($nodes_address)
    IFS="${OLD_IFS}"
    for node in ${nodes[@]}
        do
            scp $basepath/node_deployment.sh $(getNodeIP $node):$k8s_dir/k8s-install/
            ssh $(getNodeIP $node) "mkdir -p $k8s_dir/images/calico/ && sh $k8s_dir/k8s-install/node_deployment.sh $(parseAddress $node) $service_dns_domain $pod_network_cidr $k8s_dir $log_level $e2e"
        done
fi

cat > $k8s_dir/bootstrap/bootstrap.secret.yaml << EOF
apiVersion: v1
kind: Secret
metadata:
  name: bootstrap-token-c8ad9c
  namespace: kube-system
type: bootstrap.kubernetes.io/token
stringData:
  description: "The default bootstrap token generated by 'kubelet '."
  token-id: c8ad9c
  token-secret: 2e4d610cf3e7426e
  usage-bootstrap-authentication: "true"
  usage-bootstrap-signing: "true"
  auth-extra-groups:  system:bootstrappers:default-node-token,system:bootstrappers:worker,system:bootstrappers:ingress

---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: kubelet-bootstrap
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: system:node-bootstrapper
subjects:
- apiGroup: rbac.authorization.k8s.io
  kind: Group
  name: system:bootstrappers:default-node-token
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: node-autoapprove-bootstrap
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: system:certificates.k8s.io:certificatesigningrequests:nodeclient
subjects:
- apiGroup: rbac.authorization.k8s.io
  kind: Group
  name: system:bootstrappers:default-node-token
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: node-autoapprove-certificate-rotation
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: system:certificates.k8s.io:certificatesigningrequests:selfnodeclient
subjects:
- apiGroup: rbac.authorization.k8s.io
  kind: Group
  name: system:nodes
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  annotations:
    rbac.authorization.kubernetes.io/autoupdate: "true"
  labels:
    kubernetes.io/bootstrapping: rbac-defaults
  name: system:kube-apiserver-to-kubelet
rules:
  - apiGroups:
      - ""
    resources:
      - nodes/proxy
      - nodes/stats
      - nodes/log
      - nodes/spec
      - nodes/metrics
    verbs:
      - "*"
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: system:kube-apiserver
  namespace: ""
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: system:kube-apiserver-to-kubelet
subjects:
  - apiGroup: rbac.authorization.k8s.io
    kind: User
    name: kube-apiserver
EOF

retry=0
while [[ $retry -le 10 ]];do
    set +e
    kubectl get clusterrolebinding >/dev/null 2>&1
    err=$?
    set -e
    if [[ $err -eq 0 ]];then
        break
    fi
    retry=$((retry+1))
    if [[ $retry -eq 10 ]];then
        exit 1
    fi
    sleep 3
done

kubectl apply -f $k8s_dir/bootstrap/bootstrap.secret.yaml
echo "bootstrap.secret.yaml create success"

# 下载coredns镜像
coredns_url=https://cmc-szver-artifactory.cmc.tools.huawei.com/artifactory/sz-software-release/cloudbasedsoftware1664351025426/release/CaaSCore-coredns/$coredns_version/coredns-${coredns_version}_$architecture.tar.gz
#wget -nc --no-check-certificate $coredns_url -O $k8s_dir/k8s-download/coredns.tar >/dev/null 2>&1
cp $package_path/coredns-1.1.0.102_aarch64.tar.gz /root/kubernetes/k8s-download/coredns.tar
tar -xf $k8s_dir/k8s-download/coredns.tar --strip-components=1 -C $k8s_dir/images/
rename $k8s_dir/images/coredns* $k8s_dir/images/coredns.tar $k8s_dir/images/coredns*

# 下载calico镜像及yaml
#wget -nc --no-check-certificate https://cmc-szver-artifactory.cmc.tools.huawei.com/artifactory/cmc-software-release/CloudSOP/caascore/1.0.0/tools/$architecture/calico_3.27.4_linux_$architecture.zip -O $k8s_dir/images/calico.zip >/dev/null 2>&1
cp $package_path/calico_3.18.6_linux_aarch64.zip /root/kubernetes/images/calico.zip
unzip $k8s_dir/images/calico.zip -d $k8s_dir/images/
rm -rf $k8s_dir/images/calico.zip
mkdir -p $k8s_dir/calico/
mv $k8s_dir/images/calico/calico*.yaml $k8s_dir/calico/

# 本机导入镜像
ctr -n k8s.io images import $k8s_dir/images/pause-3.8-$architecture.tar
ctr -n k8s.io images import $k8s_dir/images/coredns.tar
ctr -n k8s.io images import $k8s_dir/images/calico/calico_cni_3.18.6
ctr -n k8s.io images import $k8s_dir/images/calico/calico_kube-controllers_3.18.6
ctr -n k8s.io images import $k8s_dir/images/calico/calico-node-arm_v3.18.6-build
ctr -n k8s.io images import $k8s_dir/images/calico/pod2daemon-flexvol_3.18.6

# 其他master导入镜像（如果存在）
if [ $masters_address ]; then
    IFS=","
    masters=($masters_address)
    IFS="${OLD_IFS}"
    for master in ${masters[@]}
        do
            scp $k8s_dir/images/coredns.tar $(getNodeIP $master):$k8s_dir/images/coredns.tar
            scp $k8s_dir/images/calico/* $(getNodeIP $master):$k8s_dir/images/calico/
            ssh $(getNodeIP $master) "ctr -n k8s.io images import $k8s_dir/images/pause-3.8-$architecture.tar &&
                              ctr -n k8s.io images import $k8s_dir/images/coredns.tar &&
                              ctr -n k8s.io images import $k8s_dir/images/calico/calico_cni_3.18.6 &&
                              ctr -n k8s.io images import $k8s_dir/images/calico/calico_kube-controllers_3.18.6 &&
                              ctr -n k8s.io images import $k8s_dir/images/calico/calico-node-arm_v3.18.6-build &&
                              ctr -n k8s.io images import $k8s_dir/images/calico/pod2daemon-flexvol_3.18.6"
        done
fi

# 其他node导入镜像（如果存在）
if [ $nodes_address ]; then
    IFS=","
    nodes=($nodes_address)
    IFS="${OLD_IFS}"
    for node in ${nodes[@]}
    do
        scp $k8s_dir/images/coredns.tar $(getNodeIP $node):$k8s_dir/images/coredns.tar
        scp $k8s_dir/images/calico/* $(getNodeIP $node):$k8s_dir/images/calico/
        ssh $(getNodeIP $node) "ctr -n k8s.io images import $k8s_dir/images/pause-3.8-$architecture.tar &&
                        ctr -n k8s.io images import $k8s_dir/images/coredns.tar &&
                        ctr -n k8s.io images import $k8s_dir/images/calico/calico_cni_3.18.6 &&
                        ctr -n k8s.io images import $k8s_dir/images/calico/calico_kube-controllers_3.18.6 &&
                        ctr -n k8s.io images import $k8s_dir/images/calico/calico-node-arm_v3.18.6-build &&
                        ctr -n k8s.io images import $k8s_dir/images/calico/pod2daemon-flexvol_3.18.6"
    done
fi

# 部署coredns
mkdir -p $k8s_dir/coredns/
cat > $k8s_dir/coredns/coredns.yaml << EOF
apiVersion: v1
kind: ServiceAccount
metadata:
  name: coredns
  namespace: kube-system
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  labels:
    kubernetes.io/bootstrapping: rbac-defaults
  name: system:coredns
rules:
  - apiGroups:
    - ""
    resources:
    - endpoints
    - services
    - pods
    - namespaces
    verbs:
    - list
    - watch
  - apiGroups:
    - discovery.k8s.io
    resources:
    - endpointslices
    verbs:
    - list
    - watch
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  annotations:
    rbac.authorization.kubernetes.io/autoupdate: "true"
  labels:
    kubernetes.io/bootstrapping: rbac-defaults
  name: system:coredns
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: system:coredns
subjects:
- kind: ServiceAccount
  name: coredns
  namespace: kube-system
---
apiVersion: v1
kind: ConfigMap
metadata:
  name: coredns
  namespace: kube-system
data:
  Corefile: |
    .:53 {
        errors
        health {
          lameduck 5s
        }
        ready
        kubernetes cluster.local in-addr.arpa ip6.arpa {
          fallthrough in-addr.arpa ip6.arpa
        }
        prometheus :9153
        cache 30
        loop
        reload
        loadbalance
    }
---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: coredns
  namespace: kube-system
  labels:
    k8s-app: kube-dns
    kubernetes.io/name: "CoreDNS"
spec:
  # replicas: not specified here:
  # 1. Default is 1.
  # 2. Will be tuned in real time if DNS horizontal auto-scaling is turned on.
  strategy:
    type: RollingUpdate
    rollingUpdate:
      maxUnavailable: 1
  selector:
    matchLabels:
      k8s-app: kube-dns
  template:
    metadata:
      labels:
        k8s-app: kube-dns
    spec:
      priorityClassName: system-cluster-critical
      serviceAccountName: coredns
      tolerations:
        - key: "CriticalAddonsOnly"
          operator: "Exists"
      nodeSelector:
        kubernetes.io/os: linux
      affinity:
         podAntiAffinity:
           preferredDuringSchedulingIgnoredDuringExecution:
           - weight: 100
             podAffinityTerm:
               labelSelector:
                 matchExpressions:
                   - key: k8s-app
                     operator: In
                     values: ["kube-dns"]
               topologyKey: kubernetes.io/hostname
      hostNetwork: true
      containers:
      - name: coredns
        image: docker.io/library/coredns:v1.8.3
        imagePullPolicy: IfNotPresent
        resources:
          limits:
            memory: 170Mi
          requests:
            cpu: 100m
            memory: 70Mi
        args: [ "-conf", "/etc/coredns/Corefile" ]
        volumeMounts:
        - name: config-volume
          mountPath: /etc/coredns
          readOnly: true
        ports:
        - containerPort: 53
          name: dns
          protocol: UDP
        - containerPort: 53
          name: dns-tcp
          protocol: TCP
        - containerPort: 9153
          name: metrics
          protocol: TCP
        securityContext:
          allowPrivilegeEscalation: false
          capabilities:
            add:
            - NET_BIND_SERVICE
            drop:
            - all
          readOnlyRootFilesystem: true
        livenessProbe:
          httpGet:
            path: /health
            port: 8080
            scheme: HTTP
          initialDelaySeconds: 60
          timeoutSeconds: 5
          successThreshold: 1
          failureThreshold: 5
        readinessProbe:
          httpGet:
            path: /ready
            port: 8181
            scheme: HTTP
      dnsPolicy: Default
      volumes:
        - name: config-volume
          configMap:
            name: coredns
            items:
            - key: Corefile
              path: Corefile
---
apiVersion: v1
kind: Service
metadata:
  name: kube-dns
  namespace: kube-system
  annotations:
    prometheus.io/port: "9153"
    prometheus.io/scrape: "true"
  labels:
    k8s-app: kube-dns
    kubernetes.io/cluster-service: "true"
    kubernetes.io/name: "CoreDNS"
spec:
  selector:
    k8s-app: kube-dns
  clusterIP: 10.96.0.20
  ipFamilyPolicy: PreferDualStack
  ports:
  - name: dns
    port: 53
    protocol: UDP
  - name: dns-tcp
    port: 53
    protocol: TCP
  - name: metrics
    port: 9153
    protocol: TCP
EOF

#kubectl apply -f $k8s_dir/coredns/coredns.yaml

calico_cidr_v4=${pod_network_cidr%,*}
calico_cidr_v6=${pod_network_cidr#*,}
# 部署calico
if [ "$ipv6_enabled" = "true" ] || [ "$ipv6_enabled" = "TRUE" ];then
    sed -i 's#value: "interface=enp39s0"#value: "interface=eth*"#g' $k8s_dir/calico/calico-dual-stack.yaml
    sed -i "s#{CALICO_CIDR_IPV4}#$calico_cidr_v4#g" $k8s_dir/calico/calico-dual-stack.yaml
    sed -i "s#{CALICO_CIDR_IPV6}#$calico_cidr_v6#g" $k8s_dir/calico/calico-dual-stack.yaml
    kubectl apply -f $k8s_dir/calico/calico-dual-stack.yaml
else
    # sed -i 's#value: "interface=enp39s0"#value: "interface=eth*"#g' $k8s_dir/calico/calico-single-stack.yaml
    # sed -i "s#{CALICO_CIDR_IPV4}#$calico_cidr_v4#g" $k8s_dir/calico/calico-single-stack.yaml
    kubectl apply -f $k8s_dir/calico/calico.yaml
fi
sleep 20
# 设置目标和超时时间（60秒）
RESOURCE="ippool/default-ipv4-ippool"
PATCH_JSON='{"spec":{"ipipMode":"Never"}}'
END_TIME=$(($SECONDS + 120))

echo "等待 $RESOURCE 并执行 Patch..."
until kubectl patch $RESOURCE --type='merge' -p "$PATCH_JSON" &>/dev/null; do
    if [ -n $end_time ] && [ $SECONDS -ge $end_time ]; then
        echo "错误：等待超时（1分钟），资源可能不存在或 Patch 持续失败。"
        exit 1
    fi
    echo "未发现资源或 Patch 失败，5秒后重试..."
    sleep 5
done

echo "Patch 执行成功！"