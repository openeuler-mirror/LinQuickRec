cmake_minimum_required(VERSION 3.14)
project(perf_collector)

if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../common ${CMAKE_BINARY_DIR}/common)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(BRPC_INCLUDE_DIR /usr/local/include /usr/local/include/absl/strings/)
set(BRPC_LIBRARIES brpc gflags ssl crypto z pthread leveldb)
link_directories(/usr/local/lib64)

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

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

add_executable(perf_collector
    src/main.cpp
    src/puller.cpp
    src/stats_engine.cpp
    src/sqlite_store.cpp
    src/api_handlers.cpp
    src/series_manager.cpp
    src/span_tree.cpp
)

target_include_directories(perf_collector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BRPC_INCLUDE_DIR}
)

target_link_libraries(perf_collector PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    ${SQLITE3_LIB}
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
)
target_include_directories(perf_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/include
    ${BRPC_INCLUDE_DIR}
    src
)
target_link_libraries(perf_integration_test PRIVATE
    common_lib
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    ${SQLITE3_LIB}
    Threads::Threads
)
set_target_properties(perf_integration_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
