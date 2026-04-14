# Gateway 服务 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(gateway_service)

set(CMAKE_CXX_STANDARD 17)

# 查找必要的包
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(brpc REQUIRED)
find_package(absl REQUIRED)

# 生成 protobuf 文件
set(PROTO_FILE "${CMAKE_SOURCE_DIR}/proto/gateway.proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/gateway.pb.cc ${PROTO_OUT_DIR}/gateway.pb.h
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${CMAKE_SOURCE_DIR}/proto ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

# 添加可执行文件
add_executable(gateway_server
    server/gateway_server.cpp
    ${PROTO_OUT_DIR}/gateway.pb.cc
    ${PROTO_OUT_DIR}/gateway.pb.h
)

target_include_directories(gateway_server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_OUT_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
)

target_link_libraries(gateway_server PRIVATE
    brpc::brpc
    protobuf::libprotobuf
    absl::strings
    gRPC::grpc++
)

set_target_properties(gateway_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
