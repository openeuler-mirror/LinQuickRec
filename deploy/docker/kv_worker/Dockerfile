FROM linquickrec/base:latest

ARG ENTRYPOINT_FILE=deploy/docker/kv_worker/entrypoint.sh

WORKDIR /workerspace

COPY services/kv_worker/ /workerspace/

COPY ${ENTRYPOINT_FILE} /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
