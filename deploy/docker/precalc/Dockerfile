FROM linquickrec/base:latest

# === 可配置的路径参数 ===
ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/precalc_service/
ARG ENTRYPOINT_FILE=deploy/docker/precalc/entrypoint.sh

WORKDIR /app

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${SERVICE_DIR} /app/services/precalc_service/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/precalc_service -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY ${ENTRYPOINT_FILE} /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8004

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
