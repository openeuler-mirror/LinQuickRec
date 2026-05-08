# Common 库 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(common_lib)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 查找必要的包
find_package(Threads REQUIRED)
find_package(gflags REQUIRED)
find_package(fmt QUIET)

# 检查 fmt 是否找到
if (fmt_FOUND)
    message(STATUS "Found fmt library")
    add_definitions(-DHAVE_FMTLIB=1)
else()
    message(STATUS "fmt library not found, formatted logging macros will be disabled")
    add_definitions(-DHAVE_FMTLIB=0)
endif()

# 添加源文件
add_library(common_lib STATIC
    src/thread_pool/thread_pool.cpp
    src/thread_pool/global_thread_pool.cpp
    # 错误码体系
    src/error/error_code.cpp
    # 日志系统
    src/logger/config.cpp
    src/logger/logger.cpp
    src/logger/console_sink.cpp
    src/logger/file_sink.cpp
)

# 包含目录
target_include_directories(common_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接必要的库
target_link_libraries(common_lib PUBLIC
    Threads::Threads
    gflags
)

# 如果找到 fmt，链接它
if (fmt_FOUND)
    target_link_libraries(common_lib PUBLIC fmt::fmt)
endif()

# 提示信息
message(STATUS "")
message(STATUS "=== Common Library Configuration ===")
message(STATUS "Include directory: ${CMAKE_CURRENT_SOURCE_DIR}/include")
    message(STATUS "Source files: thread_pool.cpp, global_thread_pool.cpp, error/error_code.cpp, logger/*.cpp")
message(STATUS "=====================================")
message(STATUS "")

# === Tests ===
add_executable(test_error tests/test_error.cpp)
target_link_libraries(test_error PRIVATE common_lib)

add_executable(test_thread_pool tests/test_thread_pool.cpp)
target_link_libraries(test_thread_pool PRIVATE common_lib)

# === Examples ===
add_executable(error_example examples/error_example.cpp)
target_link_libraries(error_example PRIVATE common_lib)

add_executable(logger_example examples/logger_example.cpp)
target_link_libraries(logger_example PRIVATE common_lib)

add_executable(thread_pool_example examples/thread_pool_example.cpp)
target_link_libraries(thread_pool_example PRIVATE common_lib)
