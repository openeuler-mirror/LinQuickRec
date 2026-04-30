FROM brpc_base:v1.3

WORKDIR /app

COPY proto/ /app/proto/
COPY common/ /app/common/
COPY services/recall/ /app/services/recall/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/recall -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

EXPOSE 8001

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

CMD ["./build/recall_server", "--server_port=8001"]
