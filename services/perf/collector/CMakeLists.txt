cmake_minimum_required(VERSION 3.14)
project(perf_collector)

set(BRPC_INCLUDE_DIR /usr/local/include /usr/local/include/absl/strings/)
set(BRPC_LIBRARIES brpc gflags ssl crypto z pthread leveldb)
link_directories(/usr/local/lib64)

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../common ${CMAKE_BINARY_DIR}/common)
endif()
if(NOT TARGET discovery_client_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../discovery/client ${CMAKE_BINARY_DIR}/discovery_client_lib_perf)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

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
)

find_library(SQLITE3_LIB sqlite3 REQUIRED)

# Discovery proto (required by common/service_discovery.h)
set(DISCOVERY_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto/discovery.proto)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.cc ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto
         --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
         discovery.proto
    DEPENDS ${DISCOVERY_PROTO_FILE}
)

set(PERF_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto/perf_handler.proto)
set(PERF_PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/perf_handler.pb.cc)
add_custom_command(
    OUTPUT ${PERF_PROTO_SRC} ${CMAKE_CURRENT_BINARY_DIR}/perf_handler.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto
         --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
         perf_handler.proto
    DEPENDS ${PERF_PROTO_FILE}
)

set(API_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto/api_handlers.proto)
set(API_PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/api_handlers.pb.cc)
add_custom_command(
    OUTPUT ${API_PROTO_SRC} ${CMAKE_CURRENT_BINARY_DIR}/api_handlers.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto
         --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
         api_handlers.proto
    DEPENDS ${API_PROTO_FILE}
)

add_executable(perf_collector
    src/main.cpp
    src/puller.cpp
    src/stats_engine.cpp
    src/sqlite_store.cpp
    src/api_handlers.cpp
    src/series_manager.cpp
    src/span_tree.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.cc
    ${PERF_PROTO_SRC}
    ${API_PROTO_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/src/discovery/service_discovery.cpp
)

target_include_directories(perf_collector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../discovery/client/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BRPC_INCLUDE_DIR}
)

target_link_libraries(perf_collector PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    ${SQLITE3_LIB}
    discovery_client_lib
    Threads::Threads
)

set_target_properties(perf_collector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Integration test
add_executable(perf_integration_test
    tests/integration_test.cpp
    src/stats_engine.cpp
    src/sqlite_store.cpp
    src/series_manager.cpp
    src/span_tree.cpp
    src/api_handlers.cpp
    ${PERF_PROTO_SRC}
    ${API_PROTO_SRC}
)
target_include_directories(perf_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BRPC_INCLUDE_DIR}
    src
)
target_link_libraries(perf_integration_test PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    ${SQLITE3_LIB}
    Threads::Threads
)
set_target_properties(perf_integration_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
