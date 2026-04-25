#ifndef COMMON_LOGGER_SINK_CONSOLE_SINK_H
#define COMMON_LOGGER_SINK_CONSOLE_SINK_H

#include "sink.h"
#include "../config.h"
#include <iostream>
#include <mutex>

namespace common {
namespace logger {

/**
 * @brief 控制台日志输出器
 */
class ConsoleSink : public LogSink {
public:
    ConsoleSink(LogLevel level = LogLevel::INFO);
    
    void Write(const std::string& message) override;
    void Flush() override;
    void SetLevel(LogLevel level) override;
    LogLevel GetLevel() const override;
    
private:
    LogLevel level_;
    mutable std::mutex mutex_;
};

} // namespace logger
} // namespace common

#endif // COMMON_LOGGER_SINK_CONSOLE_SINK_H