#include "common/logger.h"
#include <thread>
#include <chrono>

int main() {
    // ==============================
    // Example 1: Default initialization (console output, INFO level)
    // ==============================
    common::logger::InitializeDefault();
    LOG_INFO_STREAM << "Service started (default config)";

    // ==============================
    // Example 2: Custom configuration
    // ==============================
    common::logger::LoggerConfig config;
    config.level = common::logger::LogLevel::DEBUG;
    config.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%T] %v";
    config.console_output = true;
    config.file_path = "service.log";
    config.max_file_size = 10 * 1024 * 1024;  // 10MB
    config.max_files = 5;
    config.enable_trace_id = true;

    common::logger::Initialize(config);

    // ==============================
    // Example 3: Stream logging at various levels
    // ==============================
    LOG_TRACE_STREAM << "Detailed trace: entering critical section";
    LOG_DEBUG_STREAM << "Debug: cache miss for key=user_12345";
    LOG_INFO_STREAM << "Request received: user_id=12345, path=/api/recommend";
    LOG_WARN_STREAM << "High latency detected: duration=2500ms, threshold=1000ms";
    LOG_ERROR_STREAM << "Failed to connect to Redis: connection refused";

    // ==============================
    // Example 4: Conditional logging
    // ==============================
    bool debug_mode = true;
    LOG_IF_STREAM(DEBUG, debug_mode) << "Debug mode enabled, dumping state...";
    LOG_IF_STREAM(TRACE, debug_mode) << "Full state dump: {...}";

    // ==============================
    // Example 5: Trace ID integration
    // ==============================
    common::logger::SetTraceIdGetter([]() {
        return "trace-abc-123-def-456";
    });
    LOG_INFO_STREAM << "Request with trace context";
    LOG_ERROR_STREAM << "Error in request processing";

    // ==============================
    // Example 6: Multiple sinks
    // ==============================
    common::logger::Logger::Instance().ClearSinks();
    common::logger::AddConsoleSink(common::logger::LogLevel::INFO);
    common::logger::AddFileSink("detailed.log", common::logger::LogLevel::TRACE);
    common::logger::AddFileSink("errors.log", common::logger::LogLevel::ERROR);

    LOG_INFO_STREAM << "Logging to both console and file";
    LOG_DEBUG_STREAM << "This debug message goes to file only";
    LOG_ERROR_STREAM << "This error goes to errors.log and console";

    // ==============================
    // Example 7: Dynamic log level
    // ==============================
    common::logger::SetLevel(common::logger::LogLevel::WARN);
    LOG_INFO_STREAM << "This won't appear (level is WARN)";
    LOG_WARN_STREAM << "This will appear (level is WARN)";
    LOG_ERROR_STREAM << "This will also appear";

    std::cout << "Logger examples completed. Check log files." << std::endl;
    return 0;
}
