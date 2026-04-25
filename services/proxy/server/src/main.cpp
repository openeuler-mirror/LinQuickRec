#include "gateway_server.h"
#include "common/global_thread_pool.h"

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <thread>

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        int calculated_size = std::max(4, cpu_cores * 2);
        FLAGS_global_thread_pool_size = std::min(128, calculated_size);
    }

    LOG(INFO) << "Proxy Service starting...";
    LOG(INFO) << "CPU cores: " << cpu_cores
              << ", Thread pool size: " << FLAGS_global_thread_pool_size;

    proxy::ProxyServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(static_cast<google::protobuf::Service*>(&service_impl),
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add ProxyService";
        return -1;
    }

    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), nullptr) != 0) {
        LOG(ERROR) << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG(INFO) << "===========================================";
    LOG(INFO) << "Proxy Service Started";
    LOG(INFO) << "===========================================";
    LOG(INFO) << "Listening on: " << server_addr;
    LOG(INFO) << "FeatureService: " << FLAGS_feature_service_addr;
    LOG(INFO) << "RecallService:  " << FLAGS_recall_service_addr;
    LOG(INFO) << "PrecalcService: " << FLAGS_precalc_service_addr;
    LOG(INFO) << "RankService:    " << FLAGS_rank_service_addr;
    LOG(INFO) << "===========================================";

    server.RunUntilAskedToQuit();

    LOG(INFO) << "Proxy Service stopped";
    return 0;
}
