# RankServiceSub 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(rank_sub_service)

# 添加 common 库
add_subdirectory(${CMAKE_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置默认构建类型为 Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "")
message(STATUS "=== RankServiceSub Build Configuration ===")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")

# ============================================================================
# BRPC 路径设置（参考 Recall 服务）
# ============================================================================
set(BRPC_INCLUDE_DIR /usr/local/include /usr/local/include/absl/strings/)
set(BRPC_LIBRARIES
    brpc
    gflags
    ssl
    crypto
    z
    pthread
)
link_directories(/usr/local/lib64)

# ============================================================================
# 查找依赖
# ============================================================================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

# Abseil 库（BRPC 依赖）
set(ABSL_LIBS
    absl_log_internal_check_op
    absl_log_internal_message
    absl_log_internal_nullguard
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
)

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
    set(YR_SDK_PATH "/usr/local/lib/python3.11/site-packages/yr/datasystem" CACHE PATH "Yuanrong SDK path")
    message(STATUS "Using Yuanrong SDK path: ${YR_SDK_PATH}")
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

# 生成 rank_sub protobuf 文件
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/rank_sub.pb.cc ${PROTO_OUT_DIR}/rank_sub.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/rank_sub.proto
    DEPENDS ${PROTO_DIR}/rank_sub.proto
)

# ============================================================================
# 编译 Server
# ============================================================================
add_executable(rank_sub_server
    server/src/rank_sub_server.cpp
    server/src/main.cpp
    ${PROTO_OUT_DIR}/rank_sub.pb.cc
)

target_include_directories(rank_sub_server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
    ${YR_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

target_link_libraries(rank_sub_server PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    Threads::Threads
    ${YR_LIBRARY}
    
    # Abseil 库 - 使用 --no-as-needed 避免 DSO missing 问题
    -Wl,--no-as-needed
    ${ABSL_LIBS}
    -Wl,--as-needed
)

set_target_properties(rank_sub_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(rank_sub_client 
    client/rank_sub_client.cpp
    ${PROTO_OUT_DIR}/rank_sub.pb.cc
)

target_include_directories(rank_sub_client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
)

target_link_libraries(rank_sub_client PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
)

set_target_properties(rank_sub_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "===========================================")
message(STATUS "RankServiceSub Build Configuration")
message(STATUS "===========================================")
message(STATUS "Build targets:")
message(STATUS "  - rank_sub_server (with Yuanrong SDK)")
message(STATUS "  - rank_sub_client")
message(STATUS "Yuanrong SDK Path: ${YR_SDK_PATH}")
message(STATUS "Yuanrong SDK Include: ${YR_INCLUDE_DIR}")
message(STATUS "Yuanrong SDK Library: ${YR_LIBRARY}")
message(STATUS "ThreadPool location: ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include")
message(STATUS "===========================================")
