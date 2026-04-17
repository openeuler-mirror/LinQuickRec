# Ranking 服务 Dockerfile
FROM lingquickrec/base:latest

WORKDIR /app

# 复制 proto 文件
COPY proto/ranking.proto /app/proto/

# 复制源代码
COPY services/ranking/CMakeLists.txt /app/
COPY services/ranking/client /app/client/
COPY services/ranking/server /app/server/

# 构建
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口
EXPOSE 8005

# 启动命令
CMD ["./build/ranking_server", "--server_port=8005"]
