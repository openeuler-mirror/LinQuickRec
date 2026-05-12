#include "rank_master_server.h"
#include "common/global_thread_pool.h"
#include <gflags/gflags.h>
#include <brpc/server.h>

#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    common::logger::LoggerConfig log_config;
    log_config.level = common::logger::LogLevel::INFO;
    log_config.console_output = true;
    log_config.file_path = "/var/log/lingquickrec/rank_master.log";
    log_config.max_file_size = 100 * 1024 * 1024;
    log_config.max_files = 5;
    log_config.enable_trace_id = true;
    common::logger::Initialize(log_config);
    
    rank::RankMasterServiceImpl rank_master_service;

    brpc::Server server;

    if (server.AddService(&rank_master_service, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG_ERROR << "Failed to add RankMasterService";
        return -1;
    }
    
    brpc::ServerOptions server_options;
    server_options.num_threads = 128;
    
    if (server.Start(FLAGS_server_port, &server_options) != 0) {
        LOG_ERROR << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }
    
    LOG_INFO << "RankMasterService started on port " << FLAGS_server_port;
    
    server.RunUntilAskedToQuit();
    
    LOG_INFO << "RankMasterService stopped";
    return 0;
}
