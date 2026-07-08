#include <brpc/server.h>
#include <gflags/gflags.h>

#include <csignal>
#include <thread>

#include "common/logger.h"
#include "common/perf_handler.h"
#include "common/perf_registry.h"
#include "common/service_discovery.h"

DEFINE_int32(server_port, 8080, "Perf-collector HTTP server port");
DEFINE_string(registry_backend, "etcd", "Registry backend: etcd or discovery_server");
DEFINE_string(discovery_addr, "127.0.0.1:8100", "Discovery server address");
DEFINE_string(etcd_endpoints, "etcd-client:2379", "etcd endpoints");
DEFINE_int32(server_num_threads, 0, "Server bthread num_threads, 0=BRPC default");
DEFINE_string(sqlite_db_path, "/var/lib/perf/perf.db", "SQLite database path");

// Forward declarations from other modules
namespace perf {

void StartPuller(
    std::shared_ptr<common::ServiceDiscovery> discovery,
    const std::string& sqlite_db_path);

} // namespace perf

static std::atomic<bool> g_running{true};

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    common::logger::LoggerConfig log_config;
    log_config.level = common::logger::LogLevel::INFO;
    log_config.console_output = true;
    log_config.file_path = "/var/log/linquickrec/perf_collector.log";
    log_config.max_file_size = 100 * 1024 * 1024;
    log_config.max_files = 5;
    log_config.enable_trace_id = true;
    common::logger::Initialize(log_config);

    common::perf::PerfRingRegistry::Instance().Init();

    LOG_INFO << "Perf-collector starting...";

    std::string backend_addr = (FLAGS_registry_backend == "etcd")
        ? FLAGS_etcd_endpoints : FLAGS_discovery_addr;
    auto discovery = std::make_shared<common::ServiceDiscovery>(
        FLAGS_registry_backend, backend_addr, 5000);

    brpc::Server server;
    if (server.AddService(new common::perf::DebugPerfService,
                          brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG_ERROR << "Failed to add DebugPerfService";
    }

    brpc::ServerOptions opts;
    if (FLAGS_server_num_threads > 0) opts.num_threads = FLAGS_server_num_threads;

    if (server.Start(FLAGS_server_port, &opts) != 0) {
        LOG_ERROR << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }
    LOG_INFO << "Perf-collector listening on port " << FLAGS_server_port;

    perf::StartPuller(discovery, FLAGS_sqlite_db_path);

    LOG_INFO << "Perf-collector stopped";
    return 0;
}
