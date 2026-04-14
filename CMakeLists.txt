cmake_minimum_required(VERSION 3.14)
project(LingQuickRec VERSION 1.0.0 LANGUAGES CXX)

# C++ 标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项
if(MSVC)
    add_compile_options(/O2)
else()
    add_compile_options(-O2)
endif()

# 查找必要的包
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(brpc REQUIRED)
find_package(absl REQUIRED)
find_package(CURL REQUIRED)

# 包含目录
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# 添加子目录
add_subdirectory(services/recall)
add_subdirectory(services/gateway)
add_subdirectory(services/feature)
add_subdirectory(services/ranking)
add_subdirectory(services/precompute)

# 公共库
add_subdirectory(common)
add_subdirectory(kvworker)
add_subdirectory(monitoring)

# 打印配置信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
