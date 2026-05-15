#include "discovery_server.h"

#include <thread>

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/logger.h"

DEFINE_int32(server_num_threads, 0,
             "Server bthread num_threads, 0 = BRPC default");
DEFINE_int32(server_idle_timeout_sec, -1,
             "Server idle connection timeout (sec), -1 = BRPC default");
DEFINE_int32(server_max_concurrency, 0,
             "Server max concurrency, 0 = no limit");

int main(int argc, char* argv[]) {
    {
        common::logger::LoggerConfig cfg;
        cfg.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%f:%L] %v";
        common::logger::Initialize(cfg);
    }

    google::ParseCommandLineFlags(&argc, &argv, true);

    LOG_INFO << "Discovery Service starting...";

    discovery::DiscoveryServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(&service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR << "Failed to add DiscoveryService";
        return -1;
    }

    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);

    brpc::ServerOptions server_options;
    if (FLAGS_server_num_threads > 0) {
        server_options.num_threads = FLAGS_server_num_threads;
    }
    if (FLAGS_server_idle_timeout_sec >= 0) {
        server_options.idle_timeout_sec = FLAGS_server_idle_timeout_sec;
    }
    if (FLAGS_server_max_concurrency > 0) {
        server_options.max_concurrency = FLAGS_server_max_concurrency;
    }

    if (server.Start(server_addr.c_str(), &server_options) != 0) {
        LOG_ERROR << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG_INFO << "===========================================";
    LOG_INFO << "Discovery Service Started";
    LOG_INFO << "===========================================";
    LOG_INFO << "Listening on: " << server_addr;
    LOG_INFO << "Heartbeat check interval: " << FLAGS_heartbeat_check_interval_ms << "ms";
    LOG_INFO << "Grace factor: " << FLAGS_heartbeat_grace_factor;
    LOG_INFO << "Cleanup factor: " << FLAGS_cleanup_factor;
    LOG_INFO << "Default heartbeat interval: " << FLAGS_default_heartbeat_interval_sec << "s";
    LOG_INFO << "===========================================";

    server.RunUntilAskedToQuit();

    LOG_INFO << "Discovery Service stopped";
    return 0;
}
