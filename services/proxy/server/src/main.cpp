#include "proxy_server.h"

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/logger.h"

DEFINE_string(registry_backend, "discovery_server",
    "Registry backend: discovery_server or etcd");
DEFINE_string(discovery_addr, "127.0.0.1:8100", "Discovery server address");
DEFINE_string(etcd_endpoints, "127.0.0.1:2379",
    "etcd endpoints, comma-separated (for etcd backend)");
DEFINE_string(feature_service_name, "feature_service", "Feature service name in discovery");
DEFINE_string(recall_service_name, "recall_service", "Recall service name in discovery");
DEFINE_string(precalc_service_name, "precalc_service", "Precalc service name in discovery");
DEFINE_string(rank_service_name, "rank_service", "Rank service name in discovery");
DEFINE_int32(discovery_refresh_interval_ms, 5000, "Discovery cache refresh interval (ms)");
DEFINE_int32(downstream_max_retries, 2, "Max retry attempts per downstream RPC (legacy)");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    common::logger::AddConsoleSink();
    common::logger::SetTraceIdGetter([]() { return proxy::get_current_trace_id(); });

    LOG_INFO << "Proxy Service starting...";

    proxy::ProxyServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(static_cast<google::protobuf::Service*>(&service_impl),
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR << "Failed to add ProxyService";
        return -1;
    }

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

    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), &server_options) != 0) {
        LOG_ERROR << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG_INFO << "===========================================";
    LOG_INFO << "Proxy Service Started";
    LOG_INFO << "===========================================";
    LOG_INFO << "Listening on: " << server_addr;
    LOG_INFO << "Discovery:    " << FLAGS_discovery_addr;
    LOG_INFO << "Discovery refresh interval: " << FLAGS_discovery_refresh_interval_ms << "ms";
    LOG_INFO << "Server num_threads: " << (FLAGS_server_num_threads > 0
             ? std::to_string(FLAGS_server_num_threads) : "default");
    LOG_INFO << "===========================================";

    server.RunUntilAskedToQuit();

    LOG_INFO << "Proxy Service stopped";
    return 0;
}
