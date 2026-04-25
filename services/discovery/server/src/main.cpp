#include "discovery_server.h"

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>

#include <thread>

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    LOG(INFO) << "Discovery Service starting...";

    discovery::DiscoveryServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(&service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add DiscoveryService";
        return -1;
    }

    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), nullptr) != 0) {
        LOG(ERROR) << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG(INFO) << "===========================================";
    LOG(INFO) << "Discovery Service Started";
    LOG(INFO) << "===========================================";
    LOG(INFO) << "Listening on: " << server_addr;
    LOG(INFO) << "Heartbeat check interval: " << FLAGS_heartbeat_check_interval_ms << "ms";
    LOG(INFO) << "Grace factor: " << FLAGS_heartbeat_grace_factor;
    LOG(INFO) << "Cleanup factor: " << FLAGS_cleanup_factor;
    LOG(INFO) << "Default heartbeat interval: " << FLAGS_default_heartbeat_interval_sec << "s";
    LOG(INFO) << "===========================================";

    server.RunUntilAskedToQuit();

    LOG(INFO) << "Discovery Service stopped";
    return 0;
}
