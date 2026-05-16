FROM linquickrec/base:latest

ARG ENTRYPOINT_FILE=deploy/docker/etcd/entrypoint.sh

COPY ${ENTRYPOINT_FILE} /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 2379 2380

ENTRYPOINT ["/entrypoint.sh"]
