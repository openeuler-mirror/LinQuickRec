cmake_minimum_required(VERSION 3.14)

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

add_library(discovery_client_lib STATIC
    src/brpc_registry_backend.cpp
    src/brpc_discovery_provider.cpp
    src/etcd_client.cpp
    src/etcd_http.cpp
    src/etcd_registry_backend.cpp
    src/etcd_discovery_provider.cpp
    src/registry_backend_factory.cpp
    src/discovery_provider_factory.cpp
    src/simple_json.cpp
)

target_include_directories(discovery_client_lib PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/..
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(discovery_client_lib PUBLIC
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
    common_lib
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(discovery_client_lib PRIVATE -Wall -Wextra -O2)
endif()
