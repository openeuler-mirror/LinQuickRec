FROM linquickrec/base:latest

ENV YR_SDK_PATH="/usr/local/lib64/python3.11/site-packages/yr/datasystem"

# === 可配置的路径参数 ===
ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/rank_sub/
ARG ENTRYPOINT_FILE=deploy/docker/rank-sub/entrypoint.sh
ARG DISCOVERY_DIR=services/discovery/

WORKDIR /app

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${SERVICE_DIR} /app/services/rank_sub/
COPY ${DISCOVERY_DIR} /app/services/discovery/

RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/rank_sub -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)
RUN mkdir -p /tmp/build_discovery && cd /tmp/build_discovery && \
    cmake /app/services/discovery -DCMAKE_BUILD_TYPE=Release && \
    make discovery_client -j$(nproc) && \
    cp bin/discovery_client /app/discovery_client && \
    rm -rf /tmp/build_discovery
RUN chmod +x /app/discovery_client

COPY ${ENTRYPOINT_FILE} /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8005

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:${LD_LIBRARY_PATH:-}

ENTRYPOINT ["/app/entrypoint.sh"]
