FROM linquickrec/base:latest

WORKDIR /app

COPY proto/ /app/proto/
COPY common/ /app/common/
COPY services/discovery/ /app/services/discovery/

RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/discovery -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) discovery_server

EXPOSE 8100

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/build/bin/discovery_server"]
CMD ["--server_port=8100"]
