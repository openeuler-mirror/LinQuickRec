cmake_minimum_required(VERSION 3.14)
project(discovery_examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

set(BRPC_INCLUDE_DIR /usr/local/include /usr/local/include/absl/strings/)
set(BRPC_LIBRARIES brpc gflags ssl crypto z pthread)
link_directories(/usr/local/lib64)

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto/discovery.proto)
set(PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.cc)
set(PROTO_HDR ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto
         --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
         discovery.proto
    DEPENDS ${PROTO_FILE}
    COMMENT "Running protoc on discovery.proto"
)

# pseudo_service — 纯 POSIX socket，无需额外链接
add_executable(pseudo_service
    pseudo_service/main.cpp
)

set_target_properties(pseudo_service PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

set(ABSL_LIBS
    absl_log_internal_check_op
    absl_log_internal_message
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
    absl_log_internal_nullguard
)

# test_discover — 需要 brpc + protobuf
add_executable(test_discover
    test_discover/main.cpp
    ${PROTO_SRC}
)

target_include_directories(test_discover PRIVATE
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(test_discover PRIVATE
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(test_discover PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(pseudo_service PRIVATE -Wall -Wextra -O2)
    target_compile_options(test_discover PRIVATE -Wall -Wextra -O2)
endif()

message(STATUS "===========================================")
message(STATUS "Discovery Examples Configuration")
message(STATUS "===========================================")
message(STATUS "Build targets:")
message(STATUS "  - pseudo_service")
message(STATUS "  - test_discover")
message(STATUS "===========================================")
