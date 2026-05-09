#ifndef COMMON_LOGGER_H
#define COMMON_LOGGER_H

// 日志系统头文件
#include "internal/logger/config.h"
#include "internal/logger/logger_core.h"
#include "internal/logger/macros.h"

// 可选：包含 sink 头文件
#include "internal/logger/sink/sink.h"
#include "internal/logger/sink/console_sink.h"
#include "internal/logger/sink/file_sink.h"

// 日志系统初始化辅助函数
namespace common {
namespace logger {

/**
 * @brief 使用默认配置初始化日志系统
 * 
 * 默认配置：
 * - 级别: INFO
 * - 输出: 控制台
 * - 文件: 不输出到文件
 */
inline void InitializeDefault() {
    Logger::Instance().InitializeDefault();
}

/**
 * @brief 使用指定配置初始化日志系统
 * @param config 日志配置
 */
inline void Initialize(const LoggerConfig& config) {
    Logger::Instance().Initialize(config);
}

/**
 * @brief 设置日志级别
 * @param level 日志级别
 */
inline void SetLevel(LogLevel level) {
    Logger::Instance().SetLevel(level);
}

/**
 * @brief 设置 trace_id 获取函数
 * @param getter 返回当前 trace_id 的函数
 */
inline void SetTraceIdGetter(std::function<std::string()> getter) {
    Logger::Instance().SetTraceIdGetter(std::move(getter));
}

/**
 * @brief 添加控制台输出器
 * @param level 日志级别过滤器
 */
inline void AddConsoleSink(LogLevel level = LogLevel::INFO) {
    Logger::Instance().AddSink(std::make_shared<ConsoleSink>(level));
}

/**
 * @brief 添加文件输出器
 * @param file_path 日志文件路径
 * @param level 日志级别过滤器
 * @param max_file_size 最大文件大小（字节）
 * @param max_files 最大文件数量
 */
inline void AddFileSink(const std::string& file_path,
                        LogLevel level = LogLevel::INFO,
                        size_t max_file_size = 100 * 1024 * 1024,
                        int max_files = 10) {
    Logger::Instance().AddSink(std::make_shared<FileSink>(
        file_path, level, max_file_size, max_files));
}

inline void ClearSinks() {
    Logger::Instance().ClearSinks();
}

} // namespace logger
} // namespace common

#endif // COMMON_LOGGER_H