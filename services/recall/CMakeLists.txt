cmake_minimum_required(VERSION 3.14)
project(vllm-brpc-gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# 添加 common 库
if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)
endif()

# ============================================================================
# BRPC 路径设置（根据您的实际安装路径调整）
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
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

# RapidJSON 路径设置
set(RAPIDJSON_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/rapidjson/include")

# ============================================================================
# 元戎 SDK 配置
# ============================================================================

if(DEFINED ENV{YR_SDK_PATH})
    set(YR_SDK_PATH $ENV{YR_SDK_PATH})
    message(STATUS "Using Yuanrong SDK from: ${YR_SDK_PATH}")
else()
    set(YR_SDK_PATH "/usr/local/lib64/python3.11/site-packages/yr/datasystem" CACHE PATH "Yuanrong SDK path")
    message(STATUS "Using Yuanrong SDK path: ${YR_SDK_PATH}")
endif()

# 设置元戎ds的头文件和库文件目录
set(YR_INCLUDE_DIR ${YR_SDK_PATH}/include)
find_library(YR_DATASYSTEM_LIB datasystem PATHS ${YR_SDK_PATH}/lib NO_DEFAULT_PATH)
if(NOT YR_DATASYSTEM_LIB)
    message(FATAL_ERROR "YR SDK library (datasystem) not found in ${YR_SDK_PATH}/lib")
endif()
message(STATUS "Yuanrong SDK include: ${YR_INCLUDE_DIR}")
message(STATUS "Yuanrong SDK library: ${YR_DATASYSTEM_LIB}")

# Discovery Client Library（ServiceDiscovery 用于 KVCache 模式）
if(NOT TARGET discovery_client_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client
                     ${CMAKE_BINARY_DIR}/discovery_client_lib_recall)
endif()

# Discovery proto（ServiceDiscovery 依赖）
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../proto)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.cc ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${PROTO_DIR} --cpp_out ${CMAKE_CURRENT_BINARY_DIR} discovery.proto
    DEPENDS ${PROTO_DIR}/discovery.proto
)

# ============================================================================
# Protobuf 代码生成
# ============================================================================

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../proto/recall.proto)
set(PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/recall.pb.cc)
set(PROTO_HDR ${CMAKE_CURRENT_BINARY_DIR}/recall.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../../proto 
         --cpp_out ${CMAKE_CURRENT_BINARY_DIR} 
         recall.proto
    DEPENDS ${PROTO_FILE}
    COMMENT "Running protoc on recall.proto"
)

add_library(recall_proto STATIC ${PROTO_SRC})
target_include_directories(recall_proto PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../proto
)
target_link_libraries(recall_proto PUBLIC ${Protobuf_LIBRARIES})

# ============================================================================
# Abseil 库
# ============================================================================
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

# ============================================================================
# 包含目录设置
# ============================================================================
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BRPC_INCLUDE_DIR}
    ${RAPIDJSON_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

# ============================================================================
# 编译 Server（使用 RapidJSON 和线程池优化）
# ============================================================================
add_executable(recall_server
    server/src/recall_server.cpp
    server/src/vllm_client.cpp
    server/src/main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.cc
)
target_include_directories(recall_server PRIVATE
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
    ${YR_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client/include
)
target_link_libraries(recall_server PRIVATE
    recall_proto
    discovery_client_lib
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    ${YR_DATASYSTEM_LIB}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(recall_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(recall_test_client 
    tests/recall_test_client.cpp
)
target_include_directories(recall_test_client PRIVATE 
    ${BRPC_INCLUDE_DIR} 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)
target_link_libraries(recall_test_client PRIVATE
    recall_proto
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(recall_test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# 编译选项
# ============================================================================
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(recall_server PRIVATE -Wall -Wextra -O2)
    target_compile_options(recall_test_client PRIVATE -Wall -Wextra -O2)
endif()

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "===========================================")
message(STATUS "Recall Service Build Configuration")
message(STATUS "===========================================")
message(STATUS "Build targets:")
message(STATUS "  - recall_server (with RapidJSON + Global ThreadPool)")
message(STATUS "  - recall_test_client")
message(STATUS "  - recall_integration_test (integration tests)")
message(STATUS "RapidJSON include dirs: ${RAPIDJSON_INCLUDE_DIRS}")
message(STATUS "ThreadPool location: ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include")
message(STATUS "===========================================")

# ============================================================================
# 编译集成测试
# ============================================================================
add_executable(recall_integration_test
    tests/recall_integration_test.cpp
    server/src/recall_server.cpp
    server/src/vllm_client.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/discovery.pb.cc
)
target_include_directories(recall_integration_test PRIVATE
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
    ${RAPIDJSON_INCLUDE_DIRS}
    ${YR_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client/include
)
target_link_libraries(recall_integration_test PRIVATE
    recall_proto
    discovery_client_lib
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    ${YR_DATASYSTEM_LIB}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(recall_integration_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(recall_integration_test PRIVATE -Wall -Wextra -O2)
endif()
