cmake_minimum_required(VERSION 3.14)
project(precalc_and_rank_master)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)

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

if(DEFINED ENV{YR_SDK_PATH})
    set(YR_SDK_PATH $ENV{YR_SDK_PATH})
else()
    set(YR_SDK_PATH "/usr/local/lib64/python3.11/site-packages/yr/datasystem" CACHE PATH "")
endif()
set(YR_INCLUDE_DIR ${YR_SDK_PATH}/include)
find_library(YR_DATASYSTEM_LIB datasystem PATHS ${YR_SDK_PATH}/lib NO_DEFAULT_PATH)
if(NOT YR_DATASYSTEM_LIB)
    message(FATAL_ERROR "YR SDK library not found in ${YR_SDK_PATH}/lib")
endif()

add_custom_command(OUTPUT ${PROTO_OUT_DIR}/precalc.pb.cc ${PROTO_OUT_DIR}/precalc.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/precalc.proto
    DEPENDS ${PROTO_DIR}/precalc.proto)
add_custom_command(OUTPUT ${PROTO_OUT_DIR}/rank_master.pb.cc ${PROTO_OUT_DIR}/rank_master.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/rank_master.proto
    DEPENDS ${PROTO_DIR}/rank_master.proto)
add_custom_command(OUTPUT ${PROTO_OUT_DIR}/rank_sub.pb.cc ${PROTO_OUT_DIR}/rank_sub.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/rank_sub.proto
    DEPENDS ${PROTO_DIR}/rank_sub.proto)
add_custom_command(OUTPUT ${PROTO_OUT_DIR}/discovery.pb.cc ${PROTO_OUT_DIR}/discovery.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PROTO_DIR}/discovery.proto
    DEPENDS ${PROTO_DIR}/discovery.proto)

set(PERF_PROTO_FILE "${PROTO_DIR}/perf_handler.proto")
set(PERF_PROTO_SRC "${PROTO_OUT_DIR}/perf_handler.pb.cc")
add_custom_command(OUTPUT ${PERF_PROTO_SRC} ${PROTO_OUT_DIR}/perf_handler.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ARGS --cpp_out=${PROTO_OUT_DIR} --proto_path=${PROTO_DIR} ${PERF_PROTO_FILE}
    DEPENDS ${PERF_PROTO_FILE})

if(NOT TARGET common_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../common ${CMAKE_BINARY_DIR}/common)
endif()
if(NOT TARGET discovery_client_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client ${CMAKE_BINARY_DIR}/discovery_client_lib_pam)
endif()

add_executable(precalc_and_rank_master
    server/src/main.cpp server/src/precalc_server.cpp server/src/rank_master_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/src/discovery/service_discovery.cpp
    ${PROTO_OUT_DIR}/precalc.pb.cc ${PROTO_OUT_DIR}/rank_master.pb.cc
    ${PROTO_OUT_DIR}/rank_sub.pb.cc ${PROTO_OUT_DIR}/discovery.pb.cc
    ${PERF_PROTO_SRC})

target_include_directories(precalc_and_rank_master PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${PROTO_OUT_DIR} ${PROTO_DIR} ${PROTOBUF_INCLUDE_DIRS} ${BRPC_INCLUDE_DIR}
    ${YR_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../discovery/client/include)

target_link_libraries(precalc_and_rank_master PRIVATE
    discovery_client_lib common_lib ${BRPC_LIBRARIES} leveldb
    ${Protobuf_LIBRARIES} ${YR_DATASYSTEM_LIB} Threads::Threads ${ABSL_LIBS})

set_target_properties(precalc_and_rank_master PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "Build target: precalc_and_rank_master")
