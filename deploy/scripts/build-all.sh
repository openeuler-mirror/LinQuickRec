#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_FILE="${PROJECT_ROOT}/deploy/docker/docker-compose.yml"

echo "======================================"
echo "Building LingQuickRec Docker Images"
echo "======================================"

cd "${PROJECT_ROOT}"
docker-compose -f ${COMPOSE_FILE} build

echo ""
echo "Build complete!"
docker images | grep -E "recall|precalc|rank"
