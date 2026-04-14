# KVWorker 服务 Dockerfile（搭载元戎）
FROM lingquickrec/base:latest

WORKDIR /app

# 复制 proto 文件
COPY kvworker/proto/kvworker.proto /app/proto/

# 复制源代码和配置
COPY kvworker/CMakeLists.txt /app/
COPY kvworker/src /app/src/
COPY kvworker/config.yaml /app/config.yaml

# 构建
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口
EXPOSE 8002

# 启动命令
CMD ["./build/kvworker_server", "--config=/app/config.yaml", "--server_port=8002"]
