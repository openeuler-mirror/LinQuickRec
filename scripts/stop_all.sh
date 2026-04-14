#!/bin/bash
# 停止所有服务的脚本

set -e

echo "======================================"
echo "LingQuickRec - 停止所有服务"
echo "======================================"

# 停止服务
docker-compose down

echo ""
echo "服务已停止！"
echo ""
