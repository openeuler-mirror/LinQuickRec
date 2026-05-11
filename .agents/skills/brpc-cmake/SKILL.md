---
name: brpc-cmake
description: All CMakeLists.txt involving brpc MUST reference this skill. brpc/absl installed via make install — no CMake config files. find_package(brpc) and find_package(absl) are forbidden. Use hardcoded BRPC_INCLUDE_DIR, BRPC_LIBRARIES (incl. leveldb), and the complete 11-item ABSL_LIBS. Includes error table & checklist.
---

## Standard CMake Template

```cmake
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

set(BRPC_INCLUDE_DIR /usr/local/include /usr/local/include/absl/strings/)
set(BRPC_LIBRARIES
    brpc
    gflags
    ssl
    crypto
    z
    pthread
    leveldb
)
link_directories(/usr/local/lib64)

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

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../proto/xxx.proto)
set(PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/xxx.pb.cc)
set(PROTO_HDR ${CMAKE_CURRENT_BINARY_DIR}/xxx.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../../proto
         --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
         xxx.proto
    DEPENDS ${PROTO_FILE}
)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BRPC_INCLUDE_DIR}
    $<IF:$<BOOL:${CMAKE_CURRENT_SOURCE_DIR}>,${CMAKE_CURRENT_SOURCE_DIR}/server/include,>
)

target_include_directories(my_target PRIVATE
    ${BRPC_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(my_target PRIVATE
    ${BRPC_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${ABSL_LIBS}
    Threads::Threads
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(my_target PRIVATE -Wall -Wextra -O2)
endif()
```

## Error-to-Library Mapping

| Missing Symbol | Missing Library | Source |
|---|---|---|
| `absl::log_internal::kCharNull` | `absl_log_internal_nullguard` | protoc-generated `.pb.cc` |
| `absl::hash_internal::MixingHashState::kSeed` | `absl_hash` | protoc-generated `.pb.cc` |
| `absl::status_internal::StatusRep::Unref` | `absl_status` | `libbrpc.a` (json_to_pb.cpp.o) |
| `absl::base_internal::SpinLockWait` | `absl_spinlock_wait` | `libbrpc.a` (protobufs_service.cpp.o) |
| `leveldb::DB::Open` / `leveldb::Options::Options` | `leveldb` | `libbrpc.a` (span.cpp.o, tracing) |

## Pre-Commit Checklist

- [ ] `find_package(Protobuf REQUIRED)` preserved
- [ ] `find_package(Threads REQUIRED)` preserved
- [ ] `find_package(brpc ...)` **removed**
- [ ] `find_package(absl ...)` **removed**
- [ ] `link_directories(/usr/local/lib64)` added
- [ ] `BRPC_INCLUDE_DIR` set
- [ ] `BRPC_LIBRARIES` includes `leveldb`
- [ ] `ABSL_LIBS` includes all 11 items (incl. `absl_spinlock_wait`)
- [ ] Every target linking brpc + proto + absl also links `${ABSL_LIBS}`
