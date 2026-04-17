# Precalc 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(precalc_service)

set(CMAKE_CXX_STANDARD 17)

# 查找必要的包
find_package(Protobuf REQUIRED)
find_package(brpc REQUIRED)

# 设置 proto 文件路径
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/../../proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# 元戎 SDK 配置
# 方法 1: 通过环境变量指定元戎 SDK 路径
if(DEFINED ENV{YR_SDK_PATH})
    set(YR_SDK_PATH $ENV{YR_SDK_PATH})
    message(STATUS "Using Yuanrong SDK from: ${YR_SDK_PATH}")
else()
    # 方法 2: 默认安装路径
    set(YR_SDK_PATH "/usr/local/yuanrong" CACHE PATH "Yuanrong SDK path")
    message(STATUS "Using default Yuanrong SDK path: ${YR_SDK_PATH}")
endif()

# 查找元戎 SDK
find_path(YR_INCLUDE_DIR datasystem/kv_client.h
    PATHS ${YR_SDK_PATH}
    PATH_SUFFIXES include
)

find_library(YR_LIBRARY
    NAMES yr-api datasystem
    PATHS ${YR_SDK_PATH}
    PATH_SUFFIXES lib lib64
)

if(NOT YR_INCLUDE_DIR OR NOT YR_LIBRARY)
    message(WARNING "Yuanrong SDK not found. Please set YR_SDK_PATH environment variable.")
    message(WARNING "Example: export YR_SDK_PATH=/path/to/yuanrong")
else()
    message(STATUS "Yuanrong SDK include: ${YR_INCLUDE_DIR}")
    message(STATUS "Yuanrong SDK library: ${YR_LIBRARY}")
endif()

# 生成 precalc protobuf 文件
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/precalc.pb.cc ${PROTO_OUT_DIR}/precalc.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/precalc.proto
    DEPENDS ${PROTO_DIR}/precalc.proto
)

# 添加可执行文件（Server）
add_executable(precalc_server
    server/src/precalc_server.cpp
    server/src/main.cpp
    ${PROTO_OUT_DIR}/precalc.pb.cc
)

target_include_directories(precalc_server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${YR_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

target_link_libraries(precalc_server PRIVATE
    brpc::brpc
    protobuf::libprotobuf
    gflags
    common_lib
    ${YR_LIBRARY}
)

set_target_properties(precalc_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(precalc_test_client 
    client/precalc_test_client.cpp
    ${PROTO_OUT_DIR}/precalc.pb.cc
)

target_include_directories(precalc_test_client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
)

target_link_libraries(precalc_test_client PRIVATE
    brpc::brpc
    protobuf::libprotobuf
    gflags
)

set_target_properties(precalc_test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 提示信息
message(STATUS "")
message(STATUS "=== PrecalcService Build Configuration ===")
message(STATUS "Yuanrong SDK Path: ${YR_SDK_PATH}")
message(STATUS "Yuanrong Include: ${YR_INCLUDE_DIR}")
message(STATUS "Yuanrong Library: ${YR_LIBRARY}")
message(STATUS "ThreadPool location: ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include")
message(STATUS "==========================================")
message(STATUS "")
