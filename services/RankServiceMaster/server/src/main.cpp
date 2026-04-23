#include "rank_master_server.h"
#include "common/global_thread_pool.h"
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <thread>

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    // 根据 CPU 核心数调整线程池大小
    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        int calculated_size = std::max(4, cpu_cores * 2);
        FLAGS_global_thread_pool_size = std::min(128, calculated_size);
    }
    
    LOG(INFO) << "CPU cores: " << cpu_cores 
              << ", Thread pool size: " << FLAGS_global_thread_pool_size;

    // 创建服务实例
    rank::RankMasterServiceImpl service_impl;

    // 创建 BRPC 服务器
    brpc::Server server;

    // 添加服务
    if (server.AddService(static_cast<google::protobuf::Service*>(&service_impl),
                         brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add RankMasterService";
        return -1;
    }

    // 启动服务器
    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), nullptr) != 0) {
        LOG(ERROR) << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG(INFO) << "===========================================";
    LOG(INFO) << "RankMaster Service Started";
    LOG(INFO) << "===========================================";
    LOG(INFO) << "Listening on: " << server_addr;
    LOG(INFO) << "Sub-worker Count: " << FLAGS_sub_worker_count;
    LOG(INFO) << "Sub-worker Addresses: " << FLAGS_sub_worker_addresses;
    LOG(INFO) << "Top-K: " << FLAGS_top_k;
    LOG(INFO) << "Payload Size: " << FLAGS_payload_size_kb << " KB";
    LOG(INFO) << "Thread Pool Size: " << FLAGS_global_thread_pool_size;
    LOG(INFO) << "Timing Stats: " << (FLAGS_enable_timing_stats ? "enabled" : "disabled");
    LOG(INFO) << "===========================================";

    // 运行直到退出
    server.RunUntilAskedToQuit();

    LOG(INFO) << "RankMaster Service stopped";
    return 0;
}
