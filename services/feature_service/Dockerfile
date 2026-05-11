# Feature 服务 Dockerfile
FROM linquickrec/base:latest

# === 可配置的路径参数 ===
ARG PROTO_FILE=proto/feature.proto
ARG CMAKELISTS_FILE=services/feature_service/CMakeLists.txt
ARG CLIENT_DIR=services/feature_service/client
ARG SERVER_DIR=services/feature_service/server

WORKDIR /app

# 复制 proto 文件
COPY ${PROTO_FILE} /app/proto/

# 复制源代码
COPY ${CMAKELISTS_FILE} /app/
COPY ${CLIENT_DIR} /app/client/
COPY ${SERVER_DIR} /app/server/

# 构建
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口
EXPOSE 8003

# 启动命令
CMD ["./build/feature_server", "--server_port=8003"]
