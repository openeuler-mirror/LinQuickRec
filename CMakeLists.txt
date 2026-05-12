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

# 包含目录
include_directories(${CMAKE_SOURCE_DIR})

# 添加公共库子目录
add_subdirectory(common)

# 添加子目�?
add_subdirectory(services/recall)
add_subdirectory(services/precalc)
add_subdirectory(services/rank_master)
add_subdirectory(services/rank_sub)
add_subdirectory(services/proxy)
add_subdirectory(services/discovery)
add_subdirectory(services/feature)

# 其他子目录
add_subdirectory(services/kv_worker)

# 打印配置信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "===========================================")
message(STATUS "Project Structure:")
message(STATUS "  - common/ (ThreadPool, utilities)")
message(STATUS "  - services/recall/")
message(STATUS "  - services/precalc/")
message(STATUS "  - services/rank_master/")
message(STATUS "  - services/rank_sub/")
message(STATUS "  - services/proxy/")
message(STATUS "  - services/discovery/")
message(STATUS "  - services/feature/")
message(STATUS "  - services/kv_worker/")
message(STATUS "===========================================")
