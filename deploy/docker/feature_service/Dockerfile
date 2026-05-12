FROM linquickrec/base:latest

ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/feature_service/
ARG ENTRYPOINT_FILE=deploy/docker/feature_service/entrypoint.sh

WORKDIR /app

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${SERVICE_DIR} /app/services/feature_service/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/feature_service -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY ${ENTRYPOINT_FILE} /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8003

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
