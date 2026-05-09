#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_FILE="${PROJECT_ROOT}/deploy/docker/docker-compose.yml"

RANK_SUB_COUNT=${1:-10}

echo "======================================"
echo "Starting LingQuickRec Services"
echo "======================================"
echo "RankSub instances: ${RANK_SUB_COUNT}"

cd "${PROJECT_ROOT}"

docker-compose -f ${COMPOSE_FILE} up -d --scale rank-sub-service=${RANK_SUB_COUNT} rank-sub-service

echo "Waiting for RankSub services to be ready..."
sleep 10

docker-compose -f ${COMPOSE_FILE} up -d

echo ""
echo "======================================"
echo "All services started!"
echo "======================================"
docker-compose -f ${COMPOSE_FILE} ps
echo ""
echo "Useful commands:"
echo "  View logs: docker-compose -f deploy/docker/docker-compose.yml logs -f"
echo "  Scale sub: bash deploy/scripts/scale-rank-sub.sh <count>"
echo "  Stop all:  bash deploy/scripts/stop-all.sh"
