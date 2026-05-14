# RankServiceMaster 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(rank_master_service)

# 添加 common 库
if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置默认构建类型�?Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "")
message(STATUS "=== RankServiceMaster Build Configuration ===")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")

# ============================================================================
# BRPC 路径设置（参�?Recall 服务�?
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

# Abseil 库（�?Recall 服务保持一致）
set(ABSL_LIBS
    absl_log_internal_nullguard
    absl_log_internal_check_op
    absl_log_internal_message
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
    absl_status
    absl_hash
    absl_spinlock_wait
    absl_throw_delegate
)

# 设置 proto 文件路径
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# 元戎 SDK 配置
if(DEFINED ENV{YR_SDK_PATH})
    set(YR_SDK_PATH $ENV{YR_SDK_PATH})
    message(STATUS "Using Yuanrong SDK from: ${YR_SDK_PATH}")
else()
    set(YR_SDK_PATH "/usr/local/lib/python3.11/site-packages/yr/datasystem" CACHE PATH "Yuanrong SDK path")
    message(STATUS "Using Yuanrong SDK path: ${YR_SDK_PATH}")
endif()

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

# 生成 rank_master protobuf 文件
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/rank_master.pb.cc ${PROTO_OUT_DIR}/rank_master.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/rank_master.proto
    DEPENDS ${PROTO_DIR}/rank_master.proto
)

# 生成 rank_sub protobuf 文件（主图需要调用子图）
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/rank_sub.pb.cc ${PROTO_OUT_DIR}/rank_sub.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/rank_sub.proto
    DEPENDS ${PROTO_DIR}/rank_sub.proto
)

# 生成 discovery protobuf 文件（用于动态发现 RankSub 实例）
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/discovery.pb.cc ${PROTO_OUT_DIR}/discovery.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/discovery.proto
    DEPENDS ${PROTO_DIR}/discovery.proto
)

# ============================================================================
# 编译 Server
# ============================================================================
add_executable(rank_master_server
    server/src/rank_master_server.cpp
    server/src/discovery_resolver.cpp
    server/src/main.cpp
    ${PROTO_OUT_DIR}/rank_master.pb.cc
    ${PROTO_OUT_DIR}/rank_sub.pb.cc
    ${PROTO_OUT_DIR}/discovery.pb.cc
)

target_include_directories(rank_master_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client/include
)

# Discovery Client Library
if(NOT TARGET discovery_client_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client
                     ${CMAKE_BINARY_DIR}/discovery_client_lib_rm)
endif()

target_link_libraries(rank_master_server PRIVATE
    discovery_client_lib
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    Threads::Threads
    
    # Abseil - 使用 --no-as-needed 避免 DSO missing 问题
    -Wl,--no-as-needed
    ${ABSL_LIBS}
    -Wl,--as-needed
)

set_target_properties(rank_master_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(rank_master_test_client 
    client/rank_master_test_client.cpp
    ${PROTO_OUT_DIR}/rank_master.pb.cc
    ${PROTO_OUT_DIR}/discovery.pb.cc
)

target_include_directories(rank_master_test_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
    ${YR_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client/include
)

target_link_libraries(rank_master_test_client PRIVATE
    discovery_client_lib
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    ${YR_LIBRARY}
    ${ABSL_LIBS}
)

set_target_properties(rank_master_test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "===========================================")
message(STATUS "RankServiceMaster Build Configuration")
message(STATUS "===========================================")
message(STATUS "Build targets:")
message(STATUS "  - rank_master_server")
message(STATUS "  - rank_master_test_client (with Yuanrong SDK)")
message(STATUS "  - rank_master_test (unit tests)")
message(STATUS "Yuanrong SDK include: ${YR_INCLUDE_DIR}")
message(STATUS "Yuanrong SDK library: ${YR_LIBRARY}")
message(STATUS "ThreadPool location: ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include")
message(STATUS "===========================================")

# ============================================================================
# 编译单元测试
# ============================================================================
add_executable(rank_master_test
    tests/test_rank_master.cpp
    server/src/rank_master_server.cpp
    server/src/discovery_resolver.cpp
    ${PROTO_OUT_DIR}/rank_master.pb.cc
    ${PROTO_OUT_DIR}/rank_sub.pb.cc
    ${PROTO_OUT_DIR}/discovery.pb.cc
)

target_include_directories(rank_master_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client/include
)

target_link_libraries(rank_master_test PRIVATE
    discovery_client_lib
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    Threads::Threads
    -Wl,--no-as-needed
    ${ABSL_LIBS}
    -Wl,--as-needed
)

set_target_properties(rank_master_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
