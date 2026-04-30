#include "rank_sub_server.h"
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
    log_config.file_path = "/var/log/lingquickrec/rank_sub.log";
    log_config.max_file_size = 100 * 1024 * 1024;
    log_config.max_files = 5;
    log_config.enable_trace_id = true;
    common::logger::Initialize(log_config);
    
    rank::RankSubServiceImpl rank_sub_service;

    brpc::Server server;

    if (server.AddService(&rank_sub_service, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add RankSubService";
        return -1;
    }
    
    brpc::ServerOptions server_options;
    server_options.num_threads = 128;
    
    if (server.Start(FLAGS_server_port, &server_options) != 0) {
        LOG(ERROR) << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }
    
    LOG(INFO) << "RankSubService started on port " << FLAGS_server_port;
    LOG(INFO) << "KVWorker Host: " << FLAGS_kvworker_host;
    LOG(INFO) << "KVWorker Port: " << FLAGS_kvworker_port;
    LOG(INFO) << "Thread Pool Size: " << FLAGS_global_thread_pool_size;
    LOG(INFO) << "Timing Stats: " << (FLAGS_enable_timing_stats ? "enabled" : "disabled");
    
    server.RunUntilAskedToQuit();
    
    LOG(INFO) << "RankSubService stopped";
    return 0;
}
