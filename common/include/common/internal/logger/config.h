#ifndef COMMON_LOGGER_CONFIG_H
#define COMMON_LOGGER_CONFIG_H

#include <string>

namespace common {
namespace logger {

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
    TRACE,   // 详细跟踪信息
    DEBUG,   // 调试信息
    INFO,    // 常规信息
    WARNING,  // 警告信息
    ERROR,   // 错误信息
    FATAL    // 致命错误
};

/**
 * @brief 日志级别转换为字符串
 */
const char* LogLevelToString(LogLevel level);

/**
 * @brief 字符串转换为日志级别
 * @param level_str 日志级别字符串 (不区分大小写)
 * @return 对应的 LogLevel，如果无效则返回 LogLevel::INFO
 */
LogLevel LogLevelFromString(const std::string& level_str);

/**
 * @brief 检查日志级别是否应该输出
 * @param level 要检查的日志级别
 * @param threshold 阈值级别
 * @return true 如果 level >= threshold
 */
bool ShouldLog(LogLevel level, LogLevel threshold);

/**
 * @brief 日志配置结构
 */
struct LoggerConfig {
    LogLevel level = LogLevel::INFO;
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v";
    bool console_output = true;
    std::string file_path;     // 空表示不输出到文件
    size_t max_file_size = 100 * 1024 * 1024;  // 100MB
    int max_files = 10;        // 文件轮转数量
    
    // 异步日志配置
    bool async = false;
    size_t queue_size = 10000; // 异步队列大小
    size_t flush_interval_ms = 1000; // 刷新间隔(毫秒)
    
    // 高级配置
    bool enable_trace_id = true; // 是否启用 trace_id 自动输出
};

} // namespace logger
} // namespace common

#endif // COMMON_LOGGER_CONFIG_H