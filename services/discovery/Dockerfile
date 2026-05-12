FROM brpc_base:latest

# === 可配置的路径参数 ===
ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/discovery/

WORKDIR /app

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${SERVICE_DIR} /app/services/discovery/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/discovery -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

EXPOSE 8100

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/build/bin/discovery_server"]
CMD ["--server_port=8100"]
