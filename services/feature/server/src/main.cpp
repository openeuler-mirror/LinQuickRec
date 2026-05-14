#include "feature_server.h"

#include <thread>

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/global_thread_pool.h"
#include "common/logger.h"

DEFINE_int32(server_num_threads, 0,
             "Server bthread num_threads, 0 = BRPC default");
DEFINE_int32(server_timeout_ms, 0,
             "Server-side timeout (ms), 0 = no limit");
DEFINE_int32(server_idle_timeout_sec, -1,
             "Server idle connection timeout (sec), -1 = BRPC default");
DEFINE_int32(server_max_concurrency, 0,
             "Server max concurrency, 0 = no limit");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        int calculated_size = std::max(4, cpu_cores * 2);
        FLAGS_global_thread_pool_size = std::min(128, calculated_size);
    }

    LOG_INFO << "CPU cores: " << cpu_cores
              << ", Thread pool size: " << FLAGS_global_thread_pool_size;

    LOG_INFO << "Feature Service starting...";

    feature::FeatureServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(&service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR << "Failed to add FeatureService";
        return -1;
    }

    brpc::ServerOptions server_options;
    if (FLAGS_server_num_threads > 0) {
        server_options.num_threads = FLAGS_server_num_threads;
    }
    if (FLAGS_server_timeout_ms > 0) {
        server_options.timeout_ms = FLAGS_server_timeout_ms;
    }
    if (FLAGS_server_idle_timeout_sec >= 0) {
        server_options.idle_timeout_sec = FLAGS_server_idle_timeout_sec;
    }
    if (FLAGS_server_max_concurrency > 0) {
        server_options.max_concurrency = FLAGS_server_max_concurrency;
    }

    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), &server_options) != 0) {
        LOG_ERROR << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG_INFO << "===========================================";
    LOG_INFO << "Feature Service Started";
    LOG_INFO << "===========================================";
    LOG_INFO << "Listening on: " << server_addr;
    LOG_INFO << "Global thread pool size: " << common::get_global_thread_pool().size();
    LOG_INFO << "===========================================";

    server.RunUntilAskedToQuit();

    LOG_INFO << "Feature Service stopped";
    return 0;
}
