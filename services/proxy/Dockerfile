FROM lingquickrec/base:latest

# === 可配置的路径参数 ===
ARG PROTO_PROXY=proto/proxy.proto
ARG PROTO_FEATURE=proto/feature.proto
ARG PROTO_RECALL=proto/recall.proto
ARG PROTO_PRECALC=proto/precalc.proto
ARG PROTO_RANK_MASTER=proto/rank_master.proto
ARG CMAKELISTS_FILE=services/proxy/CMakeLists.txt
ARG CLIENT_DIR=services/proxy/client
ARG SERVER_DIR=services/proxy/server

WORKDIR /app

COPY ${PROTO_PROXY} /app/proto/
COPY ${PROTO_FEATURE} /app/proto/
COPY ${PROTO_RECALL} /app/proto/
COPY ${PROTO_PRECALC} /app/proto/
COPY ${PROTO_RANK_MASTER} /app/proto/

COPY ${CMAKELISTS_FILE} /app/
COPY ${CLIENT_DIR} /app/client/
COPY ${SERVER_DIR} /app/server/

RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

EXPOSE 8080

CMD ["./build/gateway_server", "--server_port=8080", "--logtostderr"]
