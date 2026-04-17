# Common 库 CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(common_lib)

set(CMAKE_CXX_STANDARD 17)

# 查找必要的包
find_package(Threads REQUIRED)
find_package(gflags REQUIRED)

# 添加源文件
add_library(common_lib STATIC
    src/thread_pool.cpp
    src/global_thread_pool.cpp
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

# 提示信息
message(STATUS "")
message(STATUS "=== Common Library Configuration ===")
message(STATUS "Include directory: ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "Source files: thread_pool.cpp, global_thread_pool.cpp")
message(STATUS "=====================================")
message(STATUS "")
