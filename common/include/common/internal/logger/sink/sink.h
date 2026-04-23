#ifndef COMMON_LOGGER_SINK_SINK_H
#define COMMON_LOGGER_SINK_SINK_H

#include <string>
#include <memory>
#include "../config.h"

namespace common {
namespace logger {

class LogSink {
public:
    virtual ~LogSink() = default;
    
    /**
     * @brief 写入日志消息
     * @param message 格式化后的日志消息
     */
    virtual void Write(const std::string& message) = 0;
    
    /**
     * @brief 刷新缓冲区
     */
    virtual void Flush() = 0;
    
    /**
     * @brief 设置日志级别过滤器
     * @param level 最小日志级别
     */
    virtual void SetLevel(LogLevel level) = 0;
    
    /**
     * @brief 获取当前日志级别过滤器
     */
    virtual LogLevel GetLevel() const = 0;
};

using LogSinkPtr = std::shared_ptr<LogSink>;

} // namespace logger
} // namespace common

#endif // COMMON_LOGGER_SINK_SINK_H