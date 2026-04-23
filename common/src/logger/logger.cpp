#include "common/internal/logger/logger_core.h"
#include "common/internal/logger/sink/console_sink.h"
#include "common/internal/logger/sink/file_sink.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace common {
namespace logger {

Logger::Logger() {
    // 默认 trace_id 获取器返回空字符串
    trace_id_getter_ = []() { return std::string(); };
}

Logger::~Logger() {
    // 刷新所有输出器
    for (auto& sink : sinks_) {
        sink->Flush();
    }
}

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Initialize(const LoggerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        // 已初始化，重新配置
        ClearSinks();
    }
    
    config_ = config;
    
    // 创建控制台输出器
    if (config.console_output) {
        AddSink(std::make_shared<ConsoleSink>(config.level));
    }
    
    // 创建文件输出器
    if (!config.file_path.empty()) {
        try {
            auto file_sink = std::make_shared<FileSink>(
                config.file_path,
                config.level,
                config.max_file_size,
                config.max_files
            );
            AddSink(file_sink);
        } catch (const std::exception& e) {
            // 文件输出器创建失败，fallback 到控制台
            if (!config.console_output) {
                AddSink(std::make_shared<ConsoleSink>(config.level));
            }
        }
    }
    
    initialized_ = true;
}

void Logger::InitializeDefault() {
    LoggerConfig config;
    config.level = LogLevel::INFO;
    config.console_output = true;
    config.file_path = "";
    Initialize(config);
}

void Logger::AddSink(LogSinkPtr sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(sink);
}

void Logger::ClearSinks() {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.level = level;
    for (auto& sink : sinks_) {
        sink->SetLevel(level);
    }
}

LogLevel Logger::GetLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.level;
}

void Logger::SetTraceIdGetter(std::function<std::string()> getter) {
    std::lock_guard<std::mutex> lock(mutex_);
    trace_id_getter_ = std::move(getter);
}

LogStream Logger::Stream(LogLevel level, 
                         const char* file, 
                         int line, 
                         const char* func) {
    return LogStream(*this, level, file, line, func);
}

void Logger::Log(LogLevel level,
                 const char* file,
                 int line,
                 const char* func,
                 const std::string& message) {
    if (!ShouldLog(level)) {
        return;
    }
    
    std::string formatted = FormatMessage(level, file, line, func, message);
    
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& sink : sinks_) {
        if (ShouldLog(level, sink->GetLevel())) {
            sink->Write(formatted);
        }
    }
}

bool Logger::ShouldLog(LogLevel level) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logger::ShouldLog(level, config_.level);
}

std::string Logger::GetTraceId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trace_id_getter_();
}

std::string Logger::FormatMessage(LogLevel level,
                                  const char* file,
                                  int line,
                                  const char* func,
                                  const std::string& message) const {
    return ApplyPattern(config_.pattern, level, file, line, func, message);
}

std::string Logger::ApplyPattern(const std::string& pattern,
                                 LogLevel level,
                                 const char* file,
                                 int line,
                                 const char* func,
                                 const std::string& message) const {
    std::string result = pattern;
    
    // 获取当前时间
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif
    
    // 替换时间模式
    // %Y: 年, %m: 月, %d: 日
    // %H: 时, %M: 分, %S: 秒
    // %e: 毫秒
    // %l: 日志级别
    // %t: 线程ID
    // %v: 消息内容
    // %f: 文件名
    // %F: 完整文件路径
    // %L: 行号
    // %c: 函数名
    // %T: trace_id
    
    std::ostringstream oss;
    
    // 逐字符处理，避免多次替换
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] == '%' && i + 1 < result.size()) {
            char spec = result[i + 1];
            switch (spec) {
                case 'Y': // 年
                    oss << std::put_time(&tm, "%Y");
                    break;
                case 'm': // 月
                    oss << std::put_time(&tm, "%m");
                    break;
                case 'd': // 日
                    oss << std::put_time(&tm, "%d");
                    break;
                case 'H': // 时
                    oss << std::put_time(&tm, "%H");
                    break;
                case 'M': // 分
                    oss << std::put_time(&tm, "%M");
                    break;
                case 'S': // 秒
                    oss << std::put_time(&tm, "%S");
                    break;
                case 'e': // 毫秒
                    oss << std::setfill('0') << std::setw(3) << ms.count();
                    break;
                case 'l': // 日志级别
                    oss << LogLevelToString(level);
                    break;
                case 't': // 线程ID
                    oss << std::this_thread::get_id();
                    break;
                case 'v': // 消息内容
                    oss << message;
                    break;
                case 'f': // 文件名
                    {
                        std::string f(file);
                        size_t pos = f.find_last_of("/\\");
                        if (pos != std::string::npos) {
                            f = f.substr(pos + 1);
                        }
                        oss << f;
                    }
                    break;
                case 'F': // 完整文件路径
                    oss << file;
                    break;
                case 'L': // 行号
                    oss << line;
                    break;
                case 'c': // 函数名
                    oss << func;
                    break;
                case 'T': // trace_id
                    if (config_.enable_trace_id) {
                        oss << GetTraceId();
                    }
                    break;
                default:
                    // 未知模式，保留原字符
                    oss << '%' << spec;
                    break;
            }
            ++i; // 跳过模式字符
        } else {
            oss << result[i];
        }
    }
    
    return oss.str();
}

// LogStream 实现
LogStream::LogStream(Logger& logger, 
                     LogLevel level,
                     const char* file,
                     int line,
                     const char* func)
    : logger_(logger),
      level_(level),
      file_(file),
      line_(line),
      func_(func) {}

LogStream::~LogStream() {
    logger_.Log(level_, file_, line_, func_, stream_.str());
}

} // namespace logger
} // namespace common