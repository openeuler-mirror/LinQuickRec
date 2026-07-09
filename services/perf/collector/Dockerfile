FROM linquickrec/base:latest

ARG PROXY_IP=""
ARG PROXY_PORT="3128"

RUN if [ -n "$PROXY_IP" ]; then \
        echo "proxy=http://${PROXY_IP}:${PROXY_PORT}" >> /etc/yum.conf; \
    fi && \
    yum install -y sqlite-devel && \
    yum clean all

ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG PERF_SERVICE_DIR=services/perf/collector/
ARG WEBUI_DIR=services/perf/webui/

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${PERF_SERVICE_DIR} /app/services/perf/collector/
COPY ${WEBUI_DIR} /app/webui/

RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/perf/collector -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY deploy/docker/perf-collector/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8080

ENTRYPOINT ["/app/entrypoint.sh"]
