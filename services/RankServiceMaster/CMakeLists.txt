# RankServiceMaster 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(rank_master_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置默认构建类型为 Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "")
message(STATUS "=== RankServiceMaster Build Configuration ===")
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
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
)

# 设置 proto 文件路径
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/../../proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

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

# ============================================================================
# 编译 Server
# ============================================================================
add_executable(rank_master_server
    server/src/rank_master_server.cpp
    server/src/main.cpp
    ${PROTO_OUT_DIR}/rank_master.pb.cc
    ${PROTO_OUT_DIR}/rank_sub.pb.cc
)

target_include_directories(rank_master_server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

target_link_libraries(rank_master_server PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(rank_master_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(rank_master_test_client 
    client/rank_master_test_client.cpp
    ${PROTO_OUT_DIR}/rank_master.pb.cc
)

target_include_directories(rank_master_test_client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
)

target_link_libraries(rank_master_test_client PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
)

set_target_properties(rank_master_test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "===========================================")
message(STATUS "RankServiceMaster Build Configuration")
message(STATUS "===========================================")
message(STATUS "Build targets:")
message(STATUS "  - rank_master_server")
message(STATUS "  - rank_master_test_client")
message(STATUS "ThreadPool location: ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include")
message(STATUS "===========================================")
