FROM linquickrec/base:latest

# === 可配置的路径参数 ===
ARG PROTO_PROXY=proto/proxy.proto
ARG PROTO_FEATURE=proto/feature.proto
ARG PROTO_RECALL=proto/recall.proto
ARG PROTO_PRECALC=proto/precalc.proto
ARG PROTO_RANK_MASTER=proto/rank_master.proto
ARG PROTO_DISCOVERY=proto/discovery.proto
ARG CMAKELISTS_FILE=services/proxy/CMakeLists.txt
ARG CLIENT_DIR=services/proxy/client
ARG SERVER_DIR=services/proxy/server
ARG TESTS_DIR=services/proxy/tests
ARG COMMON_DIR=common
ARG DISCOVERY_CMAKE=services/discovery/CMakeLists.txt
ARG DISCOVERY_CLIENT_DIR=services/discovery/client
ARG DISCOVERY_SERVER_DIR=services/discovery/server

WORKDIR /app

# ── 复制源码 ──────────────────────────────────────────────────
COPY ${PROTO_PROXY} /app/proto/
COPY ${PROTO_FEATURE} /app/proto/
COPY ${PROTO_RECALL} /app/proto/
COPY ${PROTO_PRECALC} /app/proto/
COPY ${PROTO_RANK_MASTER} /app/proto/
COPY ${PROTO_DISCOVERY} /app/proto/

COPY ${CMAKELISTS_FILE} /app/
COPY ${CLIENT_DIR} /app/client/
COPY ${SERVER_DIR} /app/server/
COPY ${TESTS_DIR} /app/tests/
COPY ${COMMON_DIR} /app/common/

# ── 编译 proxy_server ──────────────────────────────────────────
RUN mkdir -p build && cd build && cmake .. && make -j$(nproc)

# ── 编译 discovery_client ─────────────────────────────────────
COPY ${DISCOVERY_CMAKE} /app/services/discovery/
COPY ${DISCOVERY_CLIENT_DIR} /app/services/discovery/client/
COPY ${DISCOVERY_SERVER_DIR} /app/services/discovery/server/
RUN mkdir -p build/discovery && cd build/discovery && \
    cmake /app/services/discovery -DCMAKE_BUILD_TYPE=Release && \
    make discovery_client -j$(nproc)

# ── entrypoint ────────────────────────────────────────────────
COPY services/proxy/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 8080

ENTRYPOINT ["/bin/sh", "/entrypoint.sh"]
