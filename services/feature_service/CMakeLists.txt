# Feature 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(feature_service)

set(CMAKE_CXX_STANDARD 17)

# 查找必要的包
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(brpc REQUIRED)
find_package(absl REQUIRED)
find_package(hiredis REQUIRED)
find_package(Threads REQUIRED)

# 生成 protobuf 文件
set(PROTO_FILE "${CMAKE_SOURCE_DIR}/proto/feature.proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/feature.pb.cc ${PROTO_OUT_DIR}/feature.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${CMAKE_SOURCE_DIR}/proto ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

# 添加可执行文件
add_executable(feature_server
    server/src/feature_server.cpp
    server/src/main.cpp
    ${PROTO_OUT_DIR}/feature.pb.cc
)

target_include_directories(feature_server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

target_link_libraries(feature_server PRIVATE
    brpc::brpc
    protobuf::libprotobuf
    absl::strings
    gRPC::grpc++
    hiredis::hiredis
    Threads::Threads
)

set_target_properties(feature_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

message(STATUS "")
message(STATUS "=== Feature Service Configuration ===")
message(STATUS "ThreadPool location: ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include")
message(STATUS "=====================================")
message(STATUS "")
