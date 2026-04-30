// 1. 对应的头文件
#include "recall_server.h"

// 2. 标准库头文件
#include <thread>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/server.h>
#include <gflags/gflags.h>

// 5. 本项目内其他头文件
#include "common/global_thread_pool.h"
#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    common::logger::LoggerConfig log_config;
    log_config.level = common::logger::LogLevel::INFO;
    log_config.console_output = true;
    log_config.file_path = "/var/log/lingquickrec/recall.log";
    log_config.max_file_size = 100 * 1024 * 1024;
    log_config.max_files = 5;
    log_config.enable_trace_id = true;
    common::logger::Initialize(log_config);

    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        int calculated_size = std::max(4, cpu_cores * 2);
        FLAGS_global_thread_pool_size = std::min(128, calculated_size);
    }
    
    LOG(INFO) << "CPU cores: " << cpu_cores 
              << ", Thread pool size: " << FLAGS_global_thread_pool_size;

    recall::RecallServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(&service_impl, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add RecallService";
        return -1;
    }

    brpc::ServerOptions server_options;
    server_options.num_threads = 128;

    if (server.Start(FLAGS_server_port, &server_options) != 0) {
        LOG(ERROR) << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }

    LOG(INFO) << "===========================================";
    LOG(INFO) << "Recall Service Started";
    LOG(INFO) << "===========================================";
    LOG(INFO) << "Listening on port: " << FLAGS_server_port;
    LOG(INFO) << "vLLM Base URL: " << FLAGS_vllm_base_url;
    LOG(INFO) << "vLLM Endpoint: " << FLAGS_vllm_endpoint;
    LOG(INFO) << "Model Name: " << FLAGS_model_name;
    LOG(INFO) << "Thread Pool Size: " << FLAGS_global_thread_pool_size;
    LOG(INFO) << "SKU Count: " << FLAGS_sku_count << " (default: 1000)";
    LOG(INFO) << "vLLM Timeout: " << FLAGS_vllm_timeout_ms << "ms";
    LOG(INFO) << "===========================================";

    server.RunUntilAskedToQuit();

    LOG(INFO) << "Recall Service stopped";
    return 0;
}
