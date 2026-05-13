#include "discovery_server.h"

#include <thread>

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/logger.h"

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
    if (server.Start(server_addr.c_str(), nullptr) != 0) {
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
