# Feature 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(feature_service)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(hiredis REQUIRED)

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

set(PROTO_FILE "${CMAKE_SOURCE_DIR}/proto/feature.proto")
set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/feature.pb.cc")
set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/feature.pb.h")

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND protobuf::protoc
    ARGS --proto_path=${CMAKE_SOURCE_DIR}/proto
         --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)

add_executable(feature_server
    server/src/feature_server.cpp
    server/src/main.cpp
    ${PROTO_SRC}
)

target_include_directories(feature_server PRIVATE
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
)

target_link_libraries(feature_server PRIVATE
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    hiredis::hiredis
    Threads::Threads
    common_lib
)

set_target_properties(feature_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

message(STATUS "=== Feature Service Configuration ===")
message(STATUS "Build targets:")
message(STATUS "  - feature_server")
message(STATUS "=====================================")
