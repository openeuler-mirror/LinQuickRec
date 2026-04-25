// 1. 对应的头文件
#include "recall_server.h"

// 2. 标准库头文件
#include <thread>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/server.h>
#include <gflags/gflags.h>
#include <butil/logging.h>

// 5. 本项目内其他头文件
<<<<<<< HEAD
#include "global_thread_pool.h"
=======
#include "common/global_thread_pool.h"
>>>>>>> origin/main

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        int calculated_size = std::max(4, cpu_cores * 2);
        FLAGS_global_thread_pool_size = std::min(128, calculated_size);
    }
    
    LOG(INFO) << "CPU cores: " << cpu_cores 
              << ", Thread pool size: " << FLAGS_global_thread_pool_size;

    recall::RecallServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(static_cast<google::protobuf::Service*>(&service_impl),
                         brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add RecallService";
        return -1;
    }

    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), nullptr) != 0) {
        LOG(ERROR) << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG(INFO) << "===========================================";
    LOG(INFO) << "Recall Service Started";
    LOG(INFO) << "===========================================";
    LOG(INFO) << "Listening on: " << server_addr;
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
