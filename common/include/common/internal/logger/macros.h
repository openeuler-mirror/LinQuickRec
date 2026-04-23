#ifndef COMMON_LOGGER_MACROS_H
#define COMMON_LOGGER_MACROS_H

#include "logger_core.h"

// 流式日志宏（兼容现有 LOG(LEVEL) << message 风格）
#define LOG_STREAM(level) \
    common::logger::Logger::Instance().Stream( \
        common::logger::LogLevel::level, \
        __FILE__, __LINE__, __FUNCTION__)

// 各级别流式日志快捷宏
#define LOG_TRACE_STREAM   LOG_STREAM(TRACE)
#define LOG_DEBUG_STREAM   LOG_STREAM(DEBUG)
#define LOG_INFO_STREAM    LOG_STREAM(INFO)
#define LOG_WARN_STREAM    LOG_STREAM(WARN)
#define LOG_ERROR_STREAM   LOG_STREAM(ERROR)
#define LOG_FATAL_STREAM   LOG_STREAM(FATAL)

// 条件日志宏（仅在满足条件时记录）
#define LOG_IF_STREAM(level, condition) \
    if (condition) LOG_STREAM(level)

// 格式化日志宏（需要 fmt 库支持）
#if HAVE_FMTLIB
#include <fmt/format.h>

#define LOG_TRACE(fmt, ...) \
    do { \
        if (common::logger::Logger::Instance().ShouldLog(common::logger::LogLevel::TRACE)) { \
            common::logger::Logger::Instance().Log( \
                common::logger::LogLevel::TRACE, \
                __FILE__, __LINE__, __FUNCTION__, \
                fmt::format(fmt, ##__VA_ARGS__)); \
        } \
    } while (0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        if (common::logger::Logger::Instance().ShouldLog(common::logger::LogLevel::DEBUG)) { \
            common::logger::Logger::Instance().Log( \
                common::logger::LogLevel::DEBUG, \
                __FILE__, __LINE__, __FUNCTION__, \
                fmt::format(fmt, ##__VA_ARGS__)); \
        } \
    } while (0)

#define LOG_INFO(fmt, ...) \
    do { \
        if (common::logger::Logger::Instance().ShouldLog(common::logger::LogLevel::INFO)) { \
            common::logger::Logger::Instance().Log( \
                common::logger::LogLevel::INFO, \
                __FILE__, __LINE__, __FUNCTION__, \
                fmt::format(fmt, ##__VA_ARGS__)); \
        } \
    } while (0)

#define LOG_WARN(fmt, ...) \
    do { \
        if (common::logger::Logger::Instance().ShouldLog(common::logger::LogLevel::WARN)) { \
            common::logger::Logger::Instance().Log( \
                common::logger::LogLevel::WARN, \
                __FILE__, __LINE__, __FUNCTION__, \
                fmt::format(fmt, ##__VA_ARGS__)); \
        } \
    } while (0)

#define LOG_ERROR(fmt, ...) \
    do { \
        if (common::logger::Logger::Instance().ShouldLog(common::logger::LogLevel::ERROR)) { \
            common::logger::Logger::Instance().Log( \
                common::logger::LogLevel::ERROR, \
                __FILE__, __LINE__, __FUNCTION__, \
                fmt::format(fmt, ##__VA_ARGS__)); \
        } \
    } while (0)

#define LOG_FATAL(fmt, ...) \
    do { \
        if (common::logger::Logger::Instance().ShouldLog(common::logger::LogLevel::FATAL)) { \
            common::logger::Logger::Instance().Log( \
                common::logger::LogLevel::FATAL, \
                __FILE__, __LINE__, __FUNCTION__, \
                fmt::format(fmt, ##__VA_ARGS__)); \
        } \
    } while (0)

#else
// 如果没有 fmt，格式化日志宏不可用
#define LOG_TRACE(fmt, ...)   static_assert(false, "fmt library required for formatted logging")
#define LOG_DEBUG(fmt, ...)   static_assert(false, "fmt library required for formatted logging")
#define LOG_INFO(fmt, ...)    static_assert(false, "fmt library required for formatted logging")
#define LOG_WARN(fmt, ...)    static_assert(false, "fmt library required for formatted logging")
#define LOG_ERROR(fmt, ...)   static_assert(false, "fmt library required for formatted logging")
#define LOG_FATAL(fmt, ...)   static_assert(false, "fmt library required for formatted logging")
#endif // HAVE_FMTLIB

// 兼容宏：尝试与现有 LOG(INFO) 宏共存
// 注意：如果已经包含了 butil/logging.h，这些宏可能会冲突
// 使用前确保没有包含 butil/logging.h，或者使用不同的宏名

// 条件编译开关：启用兼容模式
#ifdef COMMON_LOGGER_COMPAT_MODE
#undef LOG
#define LOG(level) LOG_STREAM(level)
#endif

#endif // COMMON_LOGGER_MACROS_H