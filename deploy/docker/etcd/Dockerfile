FROM linquickrec/base:latest

# 从 share/ 复制 etcd 离线 tar 包并安装
COPY share/etcd-v3.5.12-linux-arm64.tar.gz /tmp/etcd.tar.gz
RUN tar -zxf /tmp/etcd.tar.gz -C /tmp/ && \
    cp /tmp/etcd-v3.5.12-linux-arm64/etcd /usr/local/bin/etcd && \
    cp /tmp/etcd-v3.5.12-linux-arm64/etcdctl /usr/local/bin/etcdctl && \
    rm -rf /tmp/etcd.tar.gz /tmp/etcd-v3.5.12-linux-arm64

ARG ENTRYPOINT_FILE=deploy/docker/etcd/entrypoint.sh

COPY ${ENTRYPOINT_FILE} /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 2379 2380

ENTRYPOINT ["/entrypoint.sh"]
