# Proxy 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(proxy_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找必要的包
find_package(Protobuf REQUIRED)
find_package(brpc REQUIRED)
find_package(absl REQUIRED)
find_package(Threads REQUIRED)

# Proto 文件目录
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# ============================================================================
# 生成所有 Proto 文件
# ============================================================================

# proxy.proto
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/proxy.pb.cc ${PROTO_OUT_DIR}/proxy.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/proxy.proto
    DEPENDS ${PROTO_DIR}/proxy.proto
)

# feature.proto
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/feature.pb.cc ${PROTO_OUT_DIR}/feature.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/feature.proto
    DEPENDS ${PROTO_DIR}/feature.proto
)

# recall.proto
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/recall.pb.cc ${PROTO_OUT_DIR}/recall.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/recall.proto
    DEPENDS ${PROTO_DIR}/recall.proto
)

# precalc.proto
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/precalc.pb.cc ${PROTO_OUT_DIR}/precalc.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/precalc.proto
    DEPENDS ${PROTO_DIR}/precalc.proto
)

# rank_master.proto
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/rank_master.pb.cc ${PROTO_OUT_DIR}/rank_master.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/rank_master.proto
    DEPENDS ${PROTO_DIR}/rank_master.proto
)

# discovery.proto
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/discovery.pb.cc ${PROTO_OUT_DIR}/discovery.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/discovery.proto
    DEPENDS ${PROTO_DIR}/discovery.proto
)

# ============================================================================
# 编译 common 库（支持独立构建，如 Docker 单服务场景）
# ============================================================================
if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)
endif()

# ============================================================================
# 编译 Server
# ============================================================================
add_executable(proxy_server
    server/src/proxy_server.cpp
    server/src/main.cpp
    server/src/service_discovery.cpp
    ${PROTO_OUT_DIR}/proxy.pb.cc
    ${PROTO_OUT_DIR}/feature.pb.cc
    ${PROTO_OUT_DIR}/recall.pb.cc
    ${PROTO_OUT_DIR}/precalc.pb.cc
    ${PROTO_OUT_DIR}/rank_master.pb.cc
    ${PROTO_OUT_DIR}/discovery.pb.cc
)

target_include_directories(proxy_server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

target_link_libraries(proxy_server PRIVATE
    common_lib
    brpc::brpc
    protobuf::libprotobuf
    absl::strings
    Threads::Threads
)

set_target_properties(proxy_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(proxy_test_client
    client/proxy_test_client.cpp
    ${PROTO_OUT_DIR}/proxy.pb.cc
)

target_include_directories(proxy_test_client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
)

target_link_libraries(proxy_test_client PRIVATE
    common_lib
    brpc::brpc
    protobuf::libprotobuf
    absl::strings
    Threads::Threads
)

set_target_properties(proxy_test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 编译 Integration Test
# ============================================================================
add_executable(proxy_integration_test
    tests/integration_test.cpp
    server/src/service_discovery.cpp
    ${PROTO_OUT_DIR}/proxy.pb.cc
    ${PROTO_OUT_DIR}/feature.pb.cc
    ${PROTO_OUT_DIR}/recall.pb.cc
    ${PROTO_OUT_DIR}/precalc.pb.cc
    ${PROTO_OUT_DIR}/rank_master.pb.cc
    ${PROTO_OUT_DIR}/discovery.pb.cc
)

target_include_directories(proxy_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

target_link_libraries(proxy_integration_test PRIVATE
    common_lib
    brpc::brpc
    protobuf::libprotobuf
    absl::strings
    Threads::Threads
)

set_target_properties(proxy_integration_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "")
message(STATUS "=== Proxy Service Configuration ===")
message(STATUS "Build targets:")
message(STATUS "  - proxy_server")
message(STATUS "  - proxy_test_client")
message(STATUS "  - proxy_integration_test")
message(STATUS "Proto files:")
message(STATUS "  - proxy.proto")
message(STATUS "  - feature.proto")
message(STATUS "  - recall.proto")
message(STATUS "  - precalc.proto")
message(STATUS "  - rank_master.proto")
message(STATUS "  - discovery.proto")
message(STATUS "ThreadPool location: ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include")
message(STATUS "=====================================")
message(STATUS "")
