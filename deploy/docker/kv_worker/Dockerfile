FROM linquickrec/base:latest

ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/kv_worker/
ARG DISCOVERY_DIR=services/discovery/
ARG ENTRYPOINT_FILE=deploy/docker/kv_worker/entrypoint.sh

WORKDIR /app

COPY ${COMMON_DIR} /app/common/
COPY ${DISCOVERY_DIR} /app/services/discovery/

RUN mkdir -p /tmp/build_discovery && cd /tmp/build_discovery && \
    cmake /app/services/discovery -DCMAKE_BUILD_TYPE=Release && \
    make discovery_client -j$(nproc) && \
    cp bin/discovery_client /app/discovery_client && \
    rm -rf /tmp/build_discovery
RUN chmod +x /app/discovery_client

COPY ${SERVICE_DIR} /workerspace/
COPY ${ENTRYPOINT_FILE} /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
