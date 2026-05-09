#!/bin/bash
set -e

SCALE_COUNT=${1:-10}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_FILE="${PROJECT_ROOT}/deploy/docker/docker-compose.yml"

echo "======================================"
echo "Scaling RankSub to ${SCALE_COUNT} instances"
echo "======================================"

cd "${PROJECT_ROOT}"
docker-compose -f ${COMPOSE_FILE} up -d --scale rank-sub-service=${SCALE_COUNT} rank-sub-service

echo "Waiting for all instances to be ready..."
sleep 5

echo ""
echo "RankSub instances:"
docker-compose -f ${COMPOSE_FILE} ps rank-sub-service

echo ""
echo "To update RankMaster's worker count, edit SUB_WORKER_COUNT in docker-compose.yml"
echo "then run: docker-compose -f ${COMPOSE_FILE} up -d --force-recreate rank-master-service"
