# Feature 服务 Dockerfile
FROM lingquickrec/base:latest

WORKDIR /app

# 复制 proto 文件
COPY proto/feature.proto /app/proto/

# 复制源代码
COPY services/feature/CMakeLists.txt /app/
COPY services/feature/client /app/client/
COPY services/feature/server /app/server/

# 构建
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口
EXPOSE 8003

# 启动命令
CMD ["./build/feature_server", "--server_port=8003"]
