#include "common/logger.h"
#include "common/error.h"
#include <iostream>

int main() {
    // 1. 测试错误码体系
    std::cout << "=== Testing Error Code System ===" << std::endl;
    
    common::error::Status ok = common::error::Status::OK();
    std::cout << "OK status: " << ok.ToString() << std::endl;
    std::cout << "IsOk: " << ok.IsOk() << std::endl;
    
    common::error::Status error = common::error::Status::Error(
        common::error::common_errors::INVALID_ARGUMENT,
        "Invalid parameter 'user_id'");
    std::cout << "Error status: " << error.ToString() << std::endl;
    std::cout << "IsError: " << error.IsError() << std::endl;
    std::cout << "Module: " << common::error::ModuleToString(error.GetModule()) << std::endl;
    std::cout << "Type: " << common::error::ErrorTypeToString(error.GetType()) << std::endl;
    
    // 2. 测试日志系统
    std::cout << "\n=== Testing Logger System ===" << std::endl;
    
    // 初始化日志系统
    common::logger::InitializeDefault();
    
    // 测试流式日志
    LOG_INFO_STREAM << "This is an info message";
    LOG_WARN_STREAM << "This is a warning message";
    LOG_ERROR_STREAM << "This is an error message";
    
    // 测试条件日志
    bool debug_enabled = false;
    LOG_IF_STREAM(DEBUG, debug_enabled) << "This debug message won't appear";
    
    debug_enabled = true;
    LOG_IF_STREAM(DEBUG, debug_enabled) << "This debug message will appear if level is DEBUG";
    
    // 设置 trace_id 获取器
    common::logger::SetTraceIdGetter([]() {
        return "test-trace-123";
    });
    
    LOG_INFO_STREAM << "Message with trace_id";
    
    // 3. 测试文件日志输出
    std::cout << "\n=== Testing File Logger ===" << std::endl;
    
    common::logger::LoggerConfig config;
    config.level = common::logger::LogLevel::DEBUG;
    config.console_output = true;
    config.file_path = "test_log.log";
    config.max_file_size = 1024; // 1KB for test
    config.max_files = 3;
    
    common::logger::Initialize(config);
    
    for (int i = 0; i < 5; ++i) {
        LOG_INFO_STREAM << "Test log entry " << i << " for file rotation test";
    }
    
    std::cout << "\n=== Test Completed ===" << std::endl;
    std::cout << "Check 'test_log.log' for file output" << std::endl;
    
    return 0;
}