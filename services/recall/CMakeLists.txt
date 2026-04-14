cmake_minimum_required(VERSION 3.14)
project(vllm-brpc-gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

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
find_package(RapidJSON REQUIRED)

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
    absl_log_internal_check_op
    absl_log_internal_message
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
)

# ============================================================================
# 包含目录设置
# ============================================================================
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BRPC_INCLUDE_DIR}
    ${RAPIDJSON_INCLUDE_DIRS}
)

# ============================================================================
# 编译 Server（使用 RapidJSON 和线程池优化）
# ============================================================================
add_executable(vllm_server 
    server/brpc_server.cpp
)
target_include_directories(vllm_server PRIVATE 
    ${BRPC_INCLUDE_DIR} 
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(vllm_server PRIVATE
    recall_proto
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
    ${RAPIDJSON_LIBRARIES}
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(vllm_client 
    client/brpc_client.cpp
)
target_include_directories(vllm_client PRIVATE 
    ${BRPC_INCLUDE_DIR} 
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(vllm_client PRIVATE
    recall_proto
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

# ============================================================================
# 编译选项
# ============================================================================
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(vllm_server PRIVATE -Wall -Wextra -O2)
    target_compile_options(vllm_client PRIVATE -Wall -Wextra -O2)
endif()

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "===========================================")
message(STATUS "Recall Service Build Configuration")
message(STATUS "===========================================")
message(STATUS "Build targets:")
message(STATUS "  - vllm_server (with RapidJSON + ThreadPool)")
message(STATUS "  - vllm_client")
message(STATUS "RapidJSON include dirs: ${RAPIDJSON_INCLUDE_DIRS}")
message(STATUS "===========================================")
