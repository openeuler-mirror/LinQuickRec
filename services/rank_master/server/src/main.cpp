#include "rank_master_server.h"

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/logger.h"

DEFINE_string(discovery_addr, "127.0.0.1:8100", "Discovery server address");
DEFINE_string(registry_backend, "discovery_server",
    "Registry backend: discovery_server or etcd");
DEFINE_string(etcd_endpoints, "127.0.0.1:2379",
    "etcd endpoints, comma-separated (for etcd backend)");
DEFINE_int32(discovery_refresh_interval_ms, 5000, "Discovery cache refresh interval (ms)");
DEFINE_int32(server_num_threads, 0,
             "Server bthread num_threads, 0 = BRPC default");
DEFINE_int32(server_idle_timeout_sec, -1,
             "Server idle connection timeout (sec), -1 = BRPC default");
DEFINE_int32(server_max_concurrency, 0,
             "Server max concurrency, 0 = no limit");

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    common::logger::LoggerConfig log_config;
    log_config.level = common::logger::LogLevel::INFO;
    log_config.console_output = true;
    log_config.file_path = "/var/log/linquickrec/rank_master.log";
    log_config.max_file_size = 100 * 1024 * 1024;
    log_config.max_files = 5;
    log_config.enable_trace_id = true;
    common::logger::Initialize(log_config);

    rank::RankMasterServiceImpl rank_master_service;

    if (!rank_master_service.IsReady()) {
        LOG_ERROR << "RankMasterServiceImpl initialization failed, shutting down";
        return -1;
    }

    brpc::Server server;

    if (server.AddService(&rank_master_service, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG_ERROR << "Failed to add RankMasterService";
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
    
    if (server.Start(FLAGS_server_port, &server_options) != 0) {
        LOG_ERROR << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }
    
    LOG_INFO << "RankMasterService started on port " << FLAGS_server_port;
    
    server.RunUntilAskedToQuit();
    
    LOG_INFO << "RankMasterService stopped";
    return 0;
}
