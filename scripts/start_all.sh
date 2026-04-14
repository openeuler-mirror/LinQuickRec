#!/bin/bash
# 启动所有服务的脚本

set -e

echo "======================================"
echo "LingQuickRec - 启动所有服务"
echo "======================================"

# 启动服务
docker-compose up -d

echo ""
echo "服务已启动！"
echo ""
echo "查看服务状态:"
echo "  docker-compose ps"
echo ""
echo "查看日志:"
echo "  docker-compose logs -f"
echo ""
echo "停止服务:"
echo "  docker-compose down"
echo ""
