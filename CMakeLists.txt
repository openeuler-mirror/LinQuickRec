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

# 查找必要的包（gRPC/brpc/absl 无 CMake Config，各子模块自行处理）
find_package(Protobuf REQUIRED)
find_package(CURL REQUIRED)

# 包含目录
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# 添加公共库子目录
add_subdirectory(common)

# 添加子目录
add_subdirectory(services/recall)
add_subdirectory(services/PrecalcService)
add_subdirectory(services/RankService)
add_subdirectory(services/RankServiceMaster)
add_subdirectory(services/RankServiceSub)
add_subdirectory(services/proxy)
add_subdirectory(services/discovery)
add_subdirectory(services/FeatureService)

# 其他子目录
add_subdirectory(kvworker)

# 打印配置信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "===========================================")
message(STATUS "Project Structure:")
message(STATUS "  - common/ (ThreadPool, utilities)")
message(STATUS "  - services/recall/")
message(STATUS "  - services/PrecalcService/")
message(STATUS "  - services/RankService/")
message(STATUS "  - services/RankServiceMaster/")
message(STATUS "  - services/RankServiceSub/")
message(STATUS "  - services/proxy/")
message(STATUS "  - services/discovery/")
message(STATUS "  - services/FeatureService/")
message(STATUS "===========================================")
