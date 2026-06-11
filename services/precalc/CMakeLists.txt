# Precalc 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(precalc_service)

# 添加 common 库
if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置默认构建类型�?Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "")
message(STATUS "=== PrecalcService Build Configuration ===")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")

# ============================================================================
# BRPC 路径设置（参�?Recall 服务�?
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
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

# Abseil 库（�?Recall 服务保持一致）
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

# 设置 proto 文件路径
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# 元戎 SDK 配置
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

# 生成 precalc protobuf 文件
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/precalc.pb.cc ${PROTO_OUT_DIR}/precalc.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/precalc.proto
    DEPENDS ${PROTO_DIR}/precalc.proto
)

# 生成 discovery protobuf 文件（用于动态发现 kv_worker）
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/discovery.pb.cc ${PROTO_OUT_DIR}/discovery.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/discovery.proto
    DEPENDS ${PROTO_DIR}/discovery.proto
)

# Discovery Client Library
if(NOT TARGET discovery_client_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client
                     ${CMAKE_BINARY_DIR}/discovery_client_lib_precalc)
endif()

# ============================================================================
# 编译 Server
# ============================================================================
add_executable(precalc_server
    server/src/precalc_server.cpp
    server/src/main.cpp
    ${PROTO_OUT_DIR}/precalc.pb.cc
    ${PROTO_OUT_DIR}/discovery.pb.cc
)

target_include_directories(precalc_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
    ${YR_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client/include
)

target_link_libraries(precalc_server PRIVATE
    discovery_client_lib
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    Threads::Threads
    ${YR_DATASYSTEM_LIB}
    ${ABSL_LIBS}
)

set_target_properties(precalc_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# 编译 Client
# ============================================================================
add_executable(precalc_test_client 
    client/precalc_test_client.cpp
    ${PROTO_OUT_DIR}/precalc.pb.cc
)

target_include_directories(precalc_test_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_OUT_DIR}
    ${PROTO_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

target_link_libraries(precalc_test_client PRIVATE
    common_lib
    ${BRPC_LIBRARIES}
    leveldb
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

set_target_properties(precalc_test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "===========================================")
message(STATUS "Precalc Service Build Configuration")
message(STATUS "===========================================")
message(STATUS "Build targets:")
message(STATUS "  - precalc_server (with Yuanrong SDK)")
message(STATUS "  - precalc_test_client")
message(STATUS "  - precalc_test (unit tests)")
message(STATUS "Yuanrong SDK include: ${YR_INCLUDE_DIR}")
message(STATUS "Yuanrong SDK library: ${YR_DATASYSTEM_LIB}")
message(STATUS "ThreadPool location: ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include")
message(STATUS "===========================================")

# ============================================================================
# 编译单元测试
# ============================================================================
add_executable(precalc_test
    tests/test_precalc.cpp
)

target_include_directories(precalc_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

target_link_libraries(precalc_test PRIVATE
    common_lib
)

set_target_properties(precalc_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
