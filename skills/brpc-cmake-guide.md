# BRPC CMake 配置指南

本项目的 brpc 通过 `make install` 编译安装（无 CMake config 文件），
abseil 同样无 CMake config。所有 CMakeLists.txt **禁止使用 `find_package`**
查找 brpc 和 absl，必须使用硬编码路径和库列表。

## 标准模板

```cmake
# 仅使用 find_package 查找 Threads 和 Protobuf（Protobuf 有 CMake config）
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

# BRPC ————————————————————————————————————
# 头文件路径
set(BRPC_INCLUDE_DIR /usr/local/include /usr/local/include/absl/strings/)
# 库列表（brpc 静态库的依赖全部展开）
set(BRPC_LIBRARIES
    brpc       # /usr/local/lib64/libbrpc.a
    gflags     # gflags 命令行参数
    ssl        # libssl.so
    crypto     # libcrypto.so
    z          # libz.so（压缩）
    pthread    # 线程
    leveldb    # brpc tracing/span 模块依赖 leveldb
)
link_directories(/usr/local/lib64)

# Abseil ———————————————————————————————————
# 完整 absl 库列表
# protoc 生成的 .pb.cc 可能引用以下任意符号，全部列出避免链接错误
set(ABSL_LIBS
    absl_log_internal_check_op
    absl_log_internal_message
    absl_raw_logging_internal
    absl_strings
    absl_base
    absl_time
    absl_synchronization
    absl_log_internal_nullguard    # protoc 生成代码引用 kCharNull
    absl_hash                      # protoc 生成代码引用 MixingHashState::kSeed
    absl_status                    # libbrpc.a(json_to_pb.cpp.o) 引用 StatusRep::Unref
)

# Protobuf 编译 ———————————————————————————
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

# 包含目录 ———————————————————————————————
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}     # protoc 输出
    ${BRPC_INCLUDE_DIR}             # brpc 头文件
    $<IF:$<BOOL:${CMAKE_CURRENT_SOURCE_DIR}>,${CMAKE_CURRENT_SOURCE_DIR}/server/include,>
)

# 目标链接 ———————————————————————————————
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

# 编译选项 ———————————————————————————————
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(my_target PRIVATE -Wall -Wextra -O2)
endif()
```

## 常见编译错误对照

| 错误符号 | 缺失库 | 来源 |
|---------|--------|------|
| `absl::log_internal::kCharNull` | `absl_log_internal_nullguard` | protoc 生成的 `.pb.cc` |
| `absl::hash_internal::MixingHashState::kSeed` | `absl_hash` | protoc 生成的 `.pb.cc` |
| `absl::status_internal::StatusRep::Unref` | `absl_status` | `libbrpc.a`（json_to_pb.cpp.o） |
| `leveldb::DB::Open` / `leveldb::Options::Options` | `leveldb` | `libbrpc.a`（span.cpp.o，tracing 模块） |

## 与 `services/recall/CMakeLists.txt` 的对比

`recall` 的 `ABSL_LIBS` 只包含前 7 项，缺少：
- `absl_log_internal_nullguard`
- `absl_hash`
- `absl_status`

这是因为 recall 的 protoc 版本或 protobuf 生成代码不同，且 recall 服务未触发 brpc 静态库中对 absl_status 的引用。  
**新模块的 CMakeLists.txt 建议直接使用上方的完整列表**，避免逐个补漏。

## 使用场景

- `services/discovery/CMakeLists.txt` — server + client 均需要 brpc + proto
- `services/discovery/examples/CMakeLists.txt` — test_discover 需要 brpc + proto；pseudo_service（纯 C++ socket）不需要
- 新增服务时直接复制上方模板

## 检查清单

- [ ] `find_package(Protobuf REQUIRED)` 保留
- [ ] `find_package(Threads REQUIRED)` 保留
- [ ] `find_package(brpc ...)` **必须删除**
- [ ] `find_package(absl ...)` **必须删除**
- [ ] `link_directories(/usr/local/lib64)` 已添加
- [ ] `BRPC_INCLUDE_DIR` 已设置
- [ ] `BRPC_LIBRARIES` 包含 `leveldb`
- [ ] `ABSL_LIBS` 包含全部 10 项
- [ ] 所有链接 `brpc` + proto + absl 的 target 都链接了 `${ABSL_LIBS}`
