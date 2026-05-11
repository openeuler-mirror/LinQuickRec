#!/bin/bash
set -e

echo "==========================================="
echo "Starting KV Worker"
echo "==========================================="

exec /workerspace/start_datasystem.sh
