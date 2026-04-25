#include "common/internal/logger/sink/console_sink.h"

namespace common {
namespace logger {

ConsoleSink::ConsoleSink(LogLevel level) : level_(level) {}

void ConsoleSink::Write(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << message << std::endl;
}

void ConsoleSink::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout.flush();
}

void ConsoleSink::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel ConsoleSink::GetLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

} // namespace logger
} // namespace common