# Precompute 服务 Dockerfile
FROM lingquickrec/base:latest

WORKDIR /app

# 复制 proto 文件
COPY proto/precompute.proto /app/proto/

# 复制源代码
COPY services/precompute/CMakeLists.txt /app/
COPY services/precompute/client /app/client/
COPY services/precompute/server /app/server/

# 构建
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口
EXPOSE 8004

# 启动命令
CMD ["./build/precompute_server", "--server_port=8004"]
