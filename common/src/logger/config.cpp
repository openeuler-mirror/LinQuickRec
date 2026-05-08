#include "common/internal/logger/config.h"
#include <algorithm>
#include <cstring>

namespace common {
namespace logger {

const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARNING:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

LogLevel LogLevelFromString(const std::string& level_str) {
    std::string upper;
    upper.reserve(level_str.size());
    for (char c : level_str) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    
    if (upper == "TRACE") return LogLevel::TRACE;
    if (upper == "DEBUG") return LogLevel::DEBUG;
    if (upper == "INFO")  return LogLevel::INFO;
    if (upper == "WARN")  return LogLevel::WARNING;
    if (upper == "ERROR") return LogLevel::ERROR;
    if (upper == "FATAL") return LogLevel::FATAL;
    
    // 默认返回 INFO
    return LogLevel::INFO;
}

bool ShouldLog(LogLevel level, LogLevel threshold) {
    return static_cast<int>(level) >= static_cast<int>(threshold);
}

} // namespace logger
} // namespace common