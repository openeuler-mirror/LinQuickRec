FROM brpc_base:latest

WORKDIR /app

COPY proto/ /app/proto/
COPY common/ /app/common/
COPY services/PrecalcService/ /app/services/PrecalcService/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/PrecalcService -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY deploy/docker/precalc/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8004

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
