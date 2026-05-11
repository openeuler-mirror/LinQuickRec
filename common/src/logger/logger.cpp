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
    trace_id_getter_ = []() { return std::string(); };
}

Logger::~Logger() {
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

    if (initialized_)
        clearSinksUnsafe();

    config_ = config;

    if (config.console_output)
        addSinkUnsafe(std::make_shared<ConsoleSink>(config.level));

    if (!config.file_path.empty()) {
        try {
            auto file_sink = std::make_shared<FileSink>(
                config.file_path,
                config.level,
                config.max_file_size,
                config.max_files
            );
            addSinkUnsafe(file_sink);
        } catch (const std::exception& e) {
            if (!config.console_output)
                addSinkUnsafe(std::make_shared<ConsoleSink>(config.level));
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

void Logger::addSinkUnsafe(LogSinkPtr sink) {
    sinks_.push_back(std::move(sink));
}

void Logger::clearSinksUnsafe() {
    sinks_.clear();
}

void Logger::AddSink(LogSinkPtr sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    addSinkUnsafe(std::move(sink));
}

void Logger::ClearSinks() {
    std::lock_guard<std::mutex> lock(mutex_);
    clearSinksUnsafe();
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
    std::string local_pattern;
    bool local_enable_trace_id = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!logger::ShouldLog(level, config_.level))
            return;
        local_pattern = config_.pattern;
        local_enable_trace_id = config_.enable_trace_id;
    }

    std::string formatted = FormatMessage(level, file, line, func, message,
                                          local_pattern, local_enable_trace_id);

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& sink : sinks_) {
        if (logger::ShouldLog(level, sink->GetLevel()))
            sink->Write(formatted);
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
                                  const std::string& message,
                                  const std::string& pattern,
                                  bool enable_trace_id) const {
    return ApplyPattern(pattern, level, file, line, func, message, enable_trace_id);
}

std::string Logger::ApplyPattern(const std::string& pattern,
                                 LogLevel level,
                                 const char* file,
                                 int line,
                                 const char* func,
                                 const std::string& message,
                                 bool enable_trace_id) const {
    std::string result = pattern;

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

    std::ostringstream oss;

    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] == '%' && i + 1 < result.size()) {
            char spec = result[i + 1];
            switch (spec) {
                case 'Y':
                    oss << std::put_time(&tm, "%Y");
                    break;
                case 'm':
                    oss << std::put_time(&tm, "%m");
                    break;
                case 'd':
                    oss << std::put_time(&tm, "%d");
                    break;
                case 'H':
                    oss << std::put_time(&tm, "%H");
                    break;
                case 'M':
                    oss << std::put_time(&tm, "%M");
                    break;
                case 'S':
                    oss << std::put_time(&tm, "%S");
                    break;
                case 'e':
                    oss << std::setfill('0') << std::setw(3) << ms.count();
                    break;
                case 'l':
                    oss << LogLevelToString(level);
                    break;
                case 't':
                    oss << std::this_thread::get_id();
                    break;
                case 'v':
                    oss << message;
                    break;
                case 'f':
                    {
                        std::string f(file);
                        size_t pos = f.find_last_of("/\\");
                        if (pos != std::string::npos)
                            f = f.substr(pos + 1);
                        oss << f;
                    }
                    break;
                case 'F':
                    oss << file;
                    break;
                case 'L':
                    oss << line;
                    break;
                case 'c':
                    oss << func;
                    break;
                case 'T':
                    if (enable_trace_id)
                        oss << GetTraceId();
                    break;
                default:
                    oss << '%' << spec;
                    break;
            }
            ++i;
        } else {
            oss << result[i];
        }
    }

    return oss.str();
}

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
