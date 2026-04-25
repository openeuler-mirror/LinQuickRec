cmake_minimum_required(VERSION 3.14)
project(discovery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# 依赖查找（单独构建时生效，作为子目录时由父工程传递）
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(brpc REQUIRED)
find_package(absl REQUIRED)

# BRPC 路径配置（当 brpc CMake config 不可用时作为降级方案）
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

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../proto/discovery.proto)
set(PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.cc)
set(PROTO_HDR ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../../proto
         --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
         discovery.proto
    DEPENDS ${PROTO_FILE}
    COMMENT "Running protoc on discovery.proto"
)

set(ABSL_LIBS
    absl_log_internal_check_op
    absl_log_internal_message
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
)

add_executable(discovery_server
    server/src/discovery_server.cpp
    server/src/main.cpp
    ${PROTO_SRC}
)

target_include_directories(discovery_server PRIVATE
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
)

target_link_libraries(discovery_server PRIVATE
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(discovery_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(discovery_client
    client/src/main.cpp
    ${PROTO_SRC}
)

target_include_directories(discovery_client PRIVATE
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(discovery_client PRIVATE
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(discovery_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(discovery_server PRIVATE -Wall -Wextra -O2)
    target_compile_options(discovery_client PRIVATE -Wall -Wextra -O2)
endif()

message(STATUS "===========================================")
message(STATUS "Discovery Build Configuration")
message(STATUS "===========================================")
message(STATUS "Build targets:")
message(STATUS "  - discovery_server")
message(STATUS "  - discovery_client")
message(STATUS "===========================================")
