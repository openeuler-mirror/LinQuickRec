FROM lingquickrec/base:latest

WORKDIR /app

COPY proto/discovery.proto /app/proto/
COPY services/discovery/CMakeLists.txt /app/
COPY services/discovery/server /app/server/
COPY services/discovery/client /app/client/

RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

EXPOSE 8100

CMD ["./build/discovery_server"]
