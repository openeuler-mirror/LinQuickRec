cmake_minimum_required(VERSION 3.14)
project(vllm-brpc-gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ===============================
# BRPC 路径设置（根据您的实际安装路径调整）
# ===============================
# 假设 brpc 安装在 /user/local，库在 /user/local/lib64
set(BRPC_INCLUDE_DIR /user/local/include /user/local/include/absl/strings/)
set(BRPC_LIBRARIES
    brpc
    gflags
    ssl
    crypto
    z
    pthread
)
link_directories(/user/local/lib64)

# =================================
# 其他依赖
# =================================
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

# =================================
# Protobuf 代码生成
# =================================
# 修改 proto 文件名
set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/recommend.proto)
set(PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/recommend.pb.cc)
set(PROTO_HDR ${CMAKE_CURRENT_BINARY_DIR}/recommend.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${CMAKE_CURRENT_SOURCE_DIR} --cpp_out ${CMAKE_CURRENT_BINARY_DIR} recommend.proto
    DEPENDS ${PROTO_FILE}
    COMMENT "Running protoc on recommend.proto"
)

add_library(recommend_proto STATIC ${PROTO_SRC})
target_include_direcotries(recommend_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(recommend_proto PUBLIC ${Protobuf_LIBRARIES})

# =================================
# Abseil 库 (根据您系统中存在的库调整，如果不确定可以先注释掉部分，报错再补)
# =================================
set(ABSL_LIBS
    absl_log_internal_check_op
    absl_log_internal_message
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
)

# =================================
# 编译Server
# =================================
include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_executable(vllm_server server/brpc_server.cpp)
target_include_direcotries(vllm_server PRIVATE ${BRPC_INCLUDE_DIR} $CMAKE_CURRENT_BINARY_DIR)
target_link_libraries(vllm_server PRIVATE
    recommend_proto
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

# =================================
# 编译Client
# =================================
add_executable(vllm_client client/brpc_client.cpp)
target_include_direcotries(vllm_client PRIVATE ${BRPC_INCLUDE_DIR} $CMAKE_CURRENT_BINARY_DIR)
target_link_libraries(vllm_client PRIVATE
    recommend_proto
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

# =================================
# 编译选项
# =================================
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(vllm_server PRIVATE -Wall -Wextra -02)
endif()