# Proxy 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(proxy_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

set(BRPC_INCLUDE_DIR /usr/local/include /usr/local/include/absl/strings/)
set(BRPC_LIBRARIES brpc gflags ssl crypto z pthread leveldb)
link_directories(/usr/local/lib64)

set(ABSL_LIBS
    absl_log_internal_check_op
    absl_log_internal_message
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
    absl_log_internal_nullguard
    absl_hash
    absl_status
    absl_spinlock_wait
)

# Proto 文件目录
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# ============================================================================
# 生成所有 Proto 文件
# ============================================================================

macro(add_proto NAME)
    add_custom_command(
        OUTPUT ${PROTO_OUT_DIR}/${NAME}.pb.cc ${PROTO_OUT_DIR}/${NAME}.pb.h
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/${NAME}.proto
        DEPENDS ${PROTO_DIR}/${NAME}.proto
    )
endmacro()

add_proto(proxy)
add_proto(feature)
add_proto(recall)
add_proto(precalc)
add_proto(rank_master)
add_proto(discovery)

# ============================================================================
# 编译 common 库（支持独立构建，如 Docker 单服务场景）
# ============================================================================
if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)
endif()

# ============================================================================
# 公共 include 和链接
# ============================================================================
set(PROTO_SRCS
    ${PROTO_OUT_DIR}/proxy.pb.cc
    ${PROTO_OUT_DIR}/feature.pb.cc
    ${PROTO_OUT_DIR}/recall.pb.cc
    ${PROTO_OUT_DIR}/precalc.pb.cc
    ${PROTO_OUT_DIR}/rank_master.pb.cc
    ${PROTO_OUT_DIR}/discovery.pb.cc
)

set(COMMON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_DIR}
    ${COMMON_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client/include
)

# ============================================================================
# Discovery Client Library
# ============================================================================
if(NOT TARGET discovery_client_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client
                     ${CMAKE_BINARY_DIR}/discovery_client_lib)
endif()

# ============================================================================
# 编译 Server
# ============================================================================
add_executable(proxy_server
    server/src/proxy_server.cpp
    server/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/src/discovery/discovery_naming_service.cpp
    ${PROTO_SRCS}
)

target_include_directories(proxy_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
)

target_link_libraries(proxy_server PRIVATE
    discovery_client_lib
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(proxy_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(proxy_test_client
    client/proxy_test_client.cpp
    ${PROTO_OUT_DIR}/proxy.pb.cc
)

target_link_libraries(proxy_test_client PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(proxy_test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# 编译 Integration Test
# ============================================================================
add_executable(proxy_integration_test
    tests/integration_test.cpp
    server/src/proxy_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/src/discovery/discovery_naming_service.cpp
    ${PROTO_SRCS}
)

target_include_directories(proxy_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
)

target_link_libraries(proxy_integration_test PRIVATE
    discovery_client_lib
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(proxy_integration_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(proxy_server PRIVATE -Wall -Wextra -O2)
    target_compile_options(proxy_test_client PRIVATE -Wall -Wextra -O2)
    target_compile_options(proxy_integration_test PRIVATE -Wall -Wextra -O2)
endif()

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
message(STATUS "=====================================")
message(STATUS "")
