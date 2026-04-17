# Precalc 服务 Dockerfile
FROM lingquickrec/base:latest

WORKDIR /app

# 复制 proto 文件
COPY proto/precalc.proto /app/proto/

# 复制源代码
COPY services/PrecalcService/CMakeLists.txt /app/
COPY services/PrecalcService/server /app/server/
COPY common /app/common/

# 构建
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口
EXPOSE 8004

# 启动命令
CMD ["./build/precalc_server", "--server_port=8004", "--kvworker_host=rank_kvworker", "--kvworker_port=8006"]
