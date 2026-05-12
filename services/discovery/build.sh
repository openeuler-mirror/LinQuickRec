#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_TYPE="Release"
CLEAN=false
CPU_CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

print_info()  { echo -e "\033[36m[INFO]\033[0m $*"; }
print_success() { echo -e "\033[32m[SUCCESS]\033[0m $*"; }
print_error()   { echo -e "\033[31m[ERROR]\033[0m $*"; }

for arg in "$@"; do
    case "$arg" in
        clean) CLEAN=true ;;
        debug) BUILD_TYPE="Debug" ;;
        release) BUILD_TYPE="Release" ;;
        relwithdebinfo) BUILD_TYPE="RelWithDebInfo" ;;
    esac
done

print_info "========================================"
print_info "Discovery Build Script"
print_info "========================================"
print_info "Build Directory: ${BUILD_DIR}"
print_info "Build Type: ${BUILD_TYPE}"
print_info "CPU Cores: ${CPU_CORES}"
print_info "========================================"

if [ "$CLEAN" = true ]; then
    print_info "Cleaning build directory..."
    if [ -d "${BUILD_DIR}" ]; then
        rm -rf "${BUILD_DIR}"
        print_success "Build directory cleaned"
    else
        print_info "Build directory does not exist, skipping clean"
    fi
fi

if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p "${BUILD_DIR}"
    print_info "Created build directory"
fi

cd "${BUILD_DIR}"

print_info "Running CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

print_info "Building with ${CPU_CORES} parallel jobs..."
cmake --build . --config ${BUILD_TYPE} -j${CPU_CORES}

if [ $? -eq 0 ]; then
    print_success "========================================"
    print_success "Build completed successfully!"
    print_success "========================================"
    print_info "Executables location: ${BUILD_DIR}/bin"
    print_info "Available executables:"
    ls -lh bin/ 2>/dev/null || true
    echo ""
    print_info "To run the server:"
    print_info "  cd ${BUILD_DIR}"
    print_info "  ./bin/discovery_server --server_port=8100"
    echo ""
else
    print_error "Build failed!"
    exit 1
fi
