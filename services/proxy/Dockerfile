FROM lingquickrec/base:latest

WORKDIR /app

COPY proto/proxy.proto /app/proto/
COPY proto/feature.proto /app/proto/
COPY proto/recall.proto /app/proto/
COPY proto/precalc.proto /app/proto/
COPY proto/rank_master.proto /app/proto/

COPY services/proxy/CMakeLists.txt /app/
COPY services/proxy/client /app/client/
COPY services/proxy/server /app/server/

RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

EXPOSE 8080

CMD ["./build/proxy_server", "--server_port=8080", "--logtostderr"]
