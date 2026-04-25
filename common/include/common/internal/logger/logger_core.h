#ifndef COMMON_LOGGER_LOGGER_H
#define COMMON_LOGGER_LOGGER_H

#include "config.h"
#include "sink/sink.h"
#include <memory>
#include <vector>
#include <mutex>
#include <string>
#include <functional>
#include <sstream>

namespace common {
namespace logger {

// 前向声明
class LogStream;

/**
 * @brief 日志记录器核心类
 * 
 * 管理日志配置和输出目标，提供线程安全的日志记录功能
 */
class Logger {
public:
    /**
     * @brief 获取日志记录器单例
     */
    static Logger& Instance();
    
    /**
     * @brief 初始化日志系统
     * @param config 日志配置
     */
    void Initialize(const LoggerConfig& config);
    
    /**
     * @brief 使用默认配置初始化日志系统
     */
    void InitializeDefault();
    
    /**
     * @brief 添加日志输出目标
     * @param sink 日志输出器
     */
    void AddSink(LogSinkPtr sink);
    
    /**
     * @brief 清空所有日志输出目标
     */
    void ClearSinks();
    
    /**
     * @brief 设置全局日志级别
     * @param level 日志级别
     */
    void SetLevel(LogLevel level);
    
    /**
     * @brief 获取全局日志级别
     */
    LogLevel GetLevel() const;
    
    /**
     * @brief 设置 trace_id 获取函数
     * @param getter 返回当前 trace_id 的函数
     */
    void SetTraceIdGetter(std::function<std::string()> getter);
    
    /**
     * @brief 创建日志流
     * @param level 日志级别
     * @param file 源文件名
     * @param line 行号
     * @param func 函数名
     * @return LogStream 日志流对象
     */
    LogStream Stream(LogLevel level, 
                     const char* file, 
                     int line, 
                     const char* func);
    
    /**
     * @brief 直接记录日志消息
     * @param level 日志级别
     * @param file 源文件名
     * @param line 行号
     * @param func 函数名
     * @param message 日志消息
     */
    void Log(LogLevel level,
             const char* file,
             int line,
             const char* func,
             const std::string& message);
    
    /**
     * @brief 检查指定级别是否应该记录
     * @param level 日志级别
     */
    bool ShouldLog(LogLevel level) const;
    
    /**
     * @brief 获取当前 trace_id
     */
    std::string GetTraceId() const;
    
private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    /**
     * @brief 格式化日志消息
     */
    std::string FormatMessage(LogLevel level,
                              const char* file,
                              int line,
                              const char* func,
                              const std::string& message) const;
    
    /**
     * @brief 应用日志模式
     */
    std::string ApplyPattern(const std::string& pattern,
                             LogLevel level,
                             const char* file,
                             int line,
                             const char* func,
                             const std::string& message) const;
    
private:
    LoggerConfig config_;
    std::vector<LogSinkPtr> sinks_;
    mutable std::mutex mutex_;
    std::function<std::string()> trace_id_getter_;
    bool initialized_ = false;
};

/**
 * @brief 日志流类，用于流式输出日志
 */
class LogStream {
public:
    LogStream(Logger& logger, 
              LogLevel level,
              const char* file,
              int line,
              const char* func);
    
    ~LogStream();
    
    // 支持流式输出
    template<typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }
    
    // 支持 std::endl 等操作符
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        stream_ << manip;
        return *this;
    }
    
private:
    Logger& logger_;
    LogLevel level_;
    const char* file_;
    int line_;
    const char* func_;
    std::ostringstream stream_;
};

} // namespace logger
} // namespace common

#endif // COMMON_LOGGER_LOGGER_H