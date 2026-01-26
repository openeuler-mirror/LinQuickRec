cmake_minimum_required(VERSION 3.14)
project(LinQuickRec C CXX)

option(LINK_SO "Whether examples are linked dynamically" OFF)

# ==================== 全局设置 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG -O2 -D__const__=__unused__ -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(CheckFunctionExists)
    CHECK_FUNCTION_EXISTS(clock_gettime HAVE_CLOCK_GETTIME)
    if(NOT HAVE_CLOCK_GETTIME)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNO_CLOCK_GETTIME_IN_MAC")
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ==================== 查找第三方库 ====================
include(FindThreads)
find_package(Protobuf REQUIRED)  # 这会处理 protobuf 和它的 Abseil 依赖
find_package(absl REQUIRED CONFIG)  # 仍然需要 find，但不需要显式链接所有目标

find_path(BRPC_INCLUDE_PATH NAMES brpc/server.h)
if(LINK_SO)
    find_library(BRPC_LIB NAMES brpc)
else()
    find_library(BRPC_LIB NAMES libbrpc.a brpc)
endif()
if((NOT BRPC_INCLUDE_PATH) OR (NOT BRPC_LIB))
    message(FATAL_ERROR "Fail to find brpc")
endif()

find_path(GFLAGS_INCLUDE_PATH gflags/gflags.h)
find_library(GFLAGS_LIBRARY NAMES gflags libgflags)
if((NOT GFLAGS_INCLUDE_PATH) OR (NOT GFLAGS_LIBRARY))
    message(FATAL_ERROR "Fail to find gflags")
endif()

find_path(LEVELDB_INCLUDE_PATH NAMES leveldb/db.h)
find_library(LEVELDB_LIB NAMES leveldb)
if((NOT LEVELDB_INCLUDE_PATH) OR (NOT LEVELDB_LIB))
    message(FATAL_ERROR "Fail to find leveldb")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
endif()
find_package(OpenSSL REQUIRED)

find_library(THRIFT_LIB NAMES thrift)
if(NOT THRIFT_LIB)
    set(THRIFT_LIB "")
endif()

find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
find_library(HIREDIS_LIB hiredis)

find_package(absl REQUIRED CONFIG)
set(ABSL_LIBS
        absl::absl_check
        absl::absl_log
        absl::algorithm
        absl::base
        absl::bind_front
        absl::bits
        absl::btree
        absl::cleanup
        absl::cord
        absl::core_headers
        absl::debugging
        absl::die_if_null
        absl::dynamic_annotations
        absl::flags
        absl::flat_hash_map
        absl::flat_hash_set
        absl::function_ref
        absl::hash
        absl::layout
        absl::log_initialize
        absl::log_severity
        absl::memory
        absl::node_hash_map
        absl::node_hash_set
        absl::optional
        absl::span
        absl::status
        absl::statusor
        absl::strings
        absl::synchronization
        absl::time
        absl::type_traits
        absl::utility
        absl::variant
)

# ==================== 创建公共依赖接口库 ====================
add_library(linquickrec_deps INTERFACE)

target_include_directories(linquickrec_deps INTERFACE
        ${BRPC_INCLUDE_PATH}
        ${GFLAGS_INCLUDE_PATH}
        ${LEVELDB_INCLUDE_PATH}
        ${OPENSSL_INCLUDE_DIR}
        ${Protobuf_INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}  # protobuf 生成目录
)

target_link_libraries(linquickrec_deps INTERFACE
        ${CMAKE_THREAD_LIBS_INIT}
        ${GFLAGS_LIBRARY}
        protobuf::libprotobuf
        ${ABSL_LIBS}
        ${LEVELDB_LIB}
        ${OPENSSL_CRYPTO_LIBRARY}
        ${OPENSSL_SSL_LIBRARY}
        ${THRIFT_LIB}
        dl
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(linquickrec_deps INTERFACE
            pthread
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework CoreData"
            "-framework CoreText"
            "-framework Security"
            "-framework Foundation"
            "-Wl,-U,_MallocExtension_ReleaseFreeMemory"
            "-Wl,-U,_ProfilerStart"
            "-Wl,-U,_ProfilerStop"
            "-Wl,-U,__Z13GetStackTracePPvii"
            "-Wl,-U,_mallctl"
            "-Wl,-U,_malloc_stats_print"
    )
endif()

# 导出变量给子目录
set(BRPC_LIB ${BRPC_LIB} CACHE INTERNAL "BRPC library")
set(HIREDIS_LIB ${HIREDIS_LIB} CACHE INTERNAL "Hiredis library")
set(HIREDIS_INCLUDE_DIR ${HIREDIS_INCLUDE_DIR} CACHE INTERNAL "Hiredis include")

# ==================== 添加子目录 ====================
add_subdirectory(proto)
add_subdirectory(cpp)