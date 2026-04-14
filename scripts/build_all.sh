#!/bin/bash
# 构建所有服务的脚本

set -e

echo "======================================"
echo "LingQuickRec - 构建所有服务"
echo "======================================"

# 1. 构建基础镜像
echo "[1/3] 构建基础镜像..."
docker build -t lingquickrec/base:latest -f Dockerfile.base .

# 2. 构建所有服务
echo "[2/3] 构建所有服务..."
docker-compose build

# 3. 显示构建结果
echo "[3/3] 构建完成！"
echo ""
echo "可用命令:"
echo "  docker-compose up              # 启动所有服务"
echo "  docker-compose up <service>    # 启动指定服务"
echo "  docker-compose down            # 停止所有服务"
echo "  docker-compose logs -f         # 查看日志"
echo ""
