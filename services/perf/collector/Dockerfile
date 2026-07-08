FROM linquickrec/base:latest

RUN apt-get update && apt-get install -y libsqlite3-dev && rm -rf /var/lib/apt/lists/*

ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG PERF_SERVICE_DIR=services/perf/collector/

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${PERF_SERVICE_DIR} /app/services/perf/collector/

RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/perf/collector -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY deploy/docker/perf-collector/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8080

ENTRYPOINT ["/app/entrypoint.sh"]
