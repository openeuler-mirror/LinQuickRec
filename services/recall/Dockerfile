FROM brpc_base:v1.3

# === 可配置的路径参数 ===
ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/recall/

WORKDIR /app

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${SERVICE_DIR} /app/services/recall/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/recall -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

EXPOSE 8001

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

CMD ["./build/recall_server", "--server_port=8001"]
