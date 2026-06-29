#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_TYPE="Release"
CLEAN=false
CPU_CORES=$(nproc 2>/dev/null || echo 4)
for arg in "$@"; do
    case "$arg" in
        clean) CLEAN=true ;;
        debug) BUILD_TYPE="Debug" ;;
        release) BUILD_TYPE="Release" ;;
    esac
done
[ "$CLEAN" = true ] && rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"
cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build . -j${CPU_CORES}
echo "Build: ${BUILD_DIR}/bin/precalc_and_rank_master"
