# Recall 服务 Dockerfile
FROM lingquickrec/base:latest

WORKDIR /app

# 复制 proto 文件
COPY proto/recall.proto /app/proto/

# 复制源代码
COPY services/recall/CMakeLists.txt /app/
COPY services/recall/client /app/client/
COPY services/recall/server /app/server/

# 构建
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口（根据实际服务端口调整）
EXPOSE 8001

# 启动命令
CMD ["./build/brpc_server", "--server_port=8001"]
