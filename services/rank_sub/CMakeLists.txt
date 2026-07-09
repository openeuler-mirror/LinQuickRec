cmake_minimum_required(VERSION 3.14)
project(rank_sub_service)

if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(BRPC_INCLUDE_DIR /usr/local/include /usr/local/include/absl/strings/)
set(BRPC_LIBRARIES brpc gflags ssl crypto z pthread)
link_directories(/usr/local/lib64)

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

set(ABSL_LIBS absl_log_internal_nullguard absl_log_internal_check_op absl_log_internal_message
    absl_raw_logging_internal absl_strings absl_base absl_time absl_synchronization
    absl_status absl_hash absl_spinlock_wait absl_throw_delegate)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

add_custom_command(OUTPUT ${PROTO_OUT_DIR}/rank_sub.pb.cc ${PROTO_OUT_DIR}/rank_sub.pb.h
    COMMAND protobuf::protoc ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/rank_sub.proto
    DEPENDS ${PROTO_DIR}/rank_sub.proto)

add_executable(rank_sub_server server/src/rank_sub_server.cpp server/src/main.cpp
    ${PROTO_OUT_DIR}/rank_sub.pb.cc)

target_include_directories(rank_sub_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_OUT_DIR} ${PROTO_DIR} ${PROTOBUF_INCLUDE_DIRS} ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include)

target_link_libraries(rank_sub_server PRIVATE common_lib ${BRPC_LIBRARIES} leveldb
    ${Protobuf_LIBRARIES} Threads::Threads ${ABSL_LIBS})

set_target_properties(rank_sub_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(rank_sub_client client/rank_sub_client.cpp ${PROTO_OUT_DIR}/rank_sub.pb.cc)
target_include_directories(rank_sub_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_OUT_DIR} ${PROTO_DIR} ${PROTOBUF_INCLUDE_DIRS} ${BRPC_INCLUDE_DIR})
target_link_libraries(rank_sub_client PRIVATE common_lib ${BRPC_LIBRARIES} leveldb
    ${Protobuf_LIBRARIES} ${ABSL_LIBS})
set_target_properties(rank_sub_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "Build targets: rank_sub_server rank_sub_client")
