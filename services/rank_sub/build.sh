#!/bin/bash

################################################################################
# RankServiceSub 编译脚本
# 使用方法�?
#   ./build.sh                    # Release 模式编译
#   ./build.sh debug              # Debug 模式编译
#   ./build.sh clean              # 清理构建
#   ./build.sh clean release      # 清理并重新编�?
################################################################################

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# 检�?CPU 核心�?
if [[ "$OSTYPE" == "darwin"* ]]; then
    CPU_CORES=$(sysctl -n hw.ncpu)
else
    CPU_CORES=$(nproc 2>/dev/null || echo 4)
fi

# 默认构建类型
BUILD_TYPE="Release"

# 解析命令行参�?
CLEAN=false
for arg in "$@"; do
    case $arg in
        clean)
            CLEAN=true
            shift
            ;;
        debug|Debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        release|Release)
            BUILD_TYPE="Release"
            shift
            ;;
        relwithdebinfo|RelWithDebInfo)
            BUILD_TYPE="RelWithDebInfo"
            shift
            ;;
        *)
            print_warning "Unknown argument: $arg"
            shift
            ;;
    esac
done

# 打印配置信息
print_info "========================================"
print_info "RankServiceSub Build Script"
print_info "========================================"
print_info "Build Directory: ${BUILD_DIR}"
print_info "Build Type: ${BUILD_TYPE}"
print_info "CPU Cores: ${CPU_CORES}"
print_info "Yuanrong SDK: /usr/local/lib/python3.11/site-packages/yr"
print_info "========================================"

# 清理构建
if [ "$CLEAN" = true ]; then
    print_info "Cleaning build directory..."
    if [ -d "${BUILD_DIR}" ]; then
        rm -rf "${BUILD_DIR}"
        print_success "Build directory cleaned"
    else
        print_info "Build directory does not exist, skipping clean"
    fi
fi

# 创建构建目录
if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p "${BUILD_DIR}"
    print_info "Created build directory"
fi

# 进入构建目录
cd "${BUILD_DIR}"

# 运行 CMake
print_info "Running CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 编译
print_info "Building with ${CPU_CORES} parallel jobs..."
cmake --build . --config ${BUILD_TYPE} -j${CPU_CORES}

# 检查编译结�?
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
    print_info "  ./bin/rank_sub_server --server_port=8006"
    echo ""
    print_info "To run the client:"
    print_info "  cd ${BUILD_DIR}"
    print_info "  ./bin/rank_sub_client --server=127.0.0.1:8006"
    echo ""
else
    print_error "Build failed!"
    exit 1
fi
