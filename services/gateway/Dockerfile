# Gateway 服务 Dockerfile
FROM lingquickrec/base:latest

WORKDIR /app

# 复制 proto 文件
COPY proto/gateway.proto /app/proto/

# 复制源代码
COPY services/gateway/CMakeLists.txt /app/
COPY services/gateway/client /app/client/
COPY services/gateway/server /app/server/

# 构建
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口
EXPOSE 8080

# 启动命令
CMD ["./build/gateway_server", "--server_port=8080"]
