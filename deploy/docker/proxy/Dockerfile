FROM linquickrec/base:latest

ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/proxy/
ARG DISCOVERY_DIR=services/discovery/
ARG ENTRYPOINT_FILE=deploy/docker/proxy/entrypoint.sh

WORKDIR /app

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${SERVICE_DIR} /app/services/proxy/
COPY ${DISCOVERY_DIR} /app/services/discovery/

RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/proxy -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) proxy_server proxy_test_client proxy_integration_test

RUN mkdir -p /app/build/discovery && cd /app/build/discovery && \
    cmake ../services/discovery -DCMAKE_BUILD_TYPE=Release && \
    make discovery_client -j$(nproc)

COPY ${ENTRYPOINT_FILE} /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8080

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
