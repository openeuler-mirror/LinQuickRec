#include "feature_server.h"

#include <thread>

#include <brpc/server.h>
#include <butil/logging.h>
#include <gflags/gflags.h>

#include "common/global_thread_pool.h"

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        int calculated_size = std::max(4, cpu_cores * 2);
        FLAGS_global_thread_pool_size = std::min(128, calculated_size);
    }

    LOG(INFO) << "CPU cores: " << cpu_cores
              << ", Thread pool size: " << FLAGS_global_thread_pool_size;

    LOG(INFO) << "Feature Service (mock) starting...";

    feature::FeatureServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(&service_impl,
                          brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add FeatureService";
        return -1;
    }

    brpc::ServerOptions server_options;
    server_options.num_threads = 128;

    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), &server_options) != 0) {
        LOG(ERROR) << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG(INFO) << "===========================================";
    LOG(INFO) << "Feature Service (mock) Started";
    LOG(INFO) << "===========================================";
    LOG(INFO) << "Listening on: " << server_addr;
    LOG(INFO) << "Global thread pool size: " << common::get_global_thread_pool().size();
    LOG(INFO) << "===========================================";

    server.RunUntilAskedToQuit();

    LOG(INFO) << "Feature Service stopped";
    return 0;
}
