#include "gateway_server.h"
#include "common/global_thread_pool.h"
#include "common/logger.h"

#include <gflags/gflags.h>
#include <brpc/server.h>
#include <thread>

DEFINE_string(discovery_addr, "127.0.0.1:8100", "Discovery server address");
DEFINE_string(feature_service_name, "feature_service", "Feature service name in discovery");
DEFINE_string(recall_service_name, "recall_service", "Recall service name in discovery");
DEFINE_string(precalc_service_name, "precalc_service", "Precalc service name in discovery");
DEFINE_string(rank_service_name, "rank_service", "Rank service name in discovery");
DEFINE_int32(discovery_refresh_interval_ms, 5000, "Discovery cache refresh interval (ms)");
DEFINE_int32(downstream_max_retries, 2, "Max retry attempts per downstream RPC");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    common::logger::AddConsoleSink();
    common::logger::SetTraceIdGetter([]() { return proxy::get_current_trace_id(); });

    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        int calculated_size = std::max(4, cpu_cores * 2);
        FLAGS_global_thread_pool_size = std::min(128, calculated_size);
    }

    LOG_INFO_STREAM << "Proxy Service starting...";
    LOG_INFO_STREAM << "CPU cores: " << cpu_cores
                    << ", Thread pool size: " << FLAGS_global_thread_pool_size;

    proxy::ProxyServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(static_cast<google::protobuf::Service*>(&service_impl),
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR_STREAM << "Failed to add ProxyService";
        return -1;
    }

    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), nullptr) != 0) {
        LOG_ERROR_STREAM << "Failed to start server on " << server_addr;
        return -1;
    }

    LOG_INFO_STREAM << "===========================================";
    LOG_INFO_STREAM << "Proxy Service Started";
    LOG_INFO_STREAM << "===========================================";
    LOG_INFO_STREAM << "Listening on: " << server_addr;
    LOG_INFO_STREAM << "Discovery:    " << FLAGS_discovery_addr;
    LOG_INFO_STREAM << "Discovery refresh interval: " << FLAGS_discovery_refresh_interval_ms << "ms";
    LOG_INFO_STREAM << "Downstream max retries: " << FLAGS_downstream_max_retries;
    LOG_INFO_STREAM << "Service names:"
                    << " feature=" << FLAGS_feature_service_name
                    << " recall=" << FLAGS_recall_service_name
                    << " precalc=" << FLAGS_precalc_service_name
                    << " rank=" << FLAGS_rank_service_name;
    LOG_INFO_STREAM << "===========================================";

    server.RunUntilAskedToQuit();

    LOG_INFO_STREAM << "Proxy Service stopped";
    return 0;
}
