#include "recall_server.h"

#include <thread>

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/global_thread_pool.h"
#define COMMON_LOGGER_COMPAT_MODE
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
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    common::logger::LoggerConfig log_config;
    log_config.level = common::logger::LogLevel::INFO;
    log_config.console_output = true;
    log_config.file_path = "/var/log/lingquickrec/recall.log";
    log_config.max_file_size = 100 * 1024 * 1024;
    log_config.max_files = 5;
    log_config.enable_trace_id = true;
    common::logger::Initialize(log_config);

    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        int calculated_size = std::max(4, cpu_cores * 2);
        FLAGS_global_thread_pool_size = std::min(128, calculated_size);
    }
    
    LOG_INFO << "CPU cores: " << cpu_cores 
              << ", Thread pool size: " << FLAGS_global_thread_pool_size;

    recall::RecallServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(&service_impl, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG_ERROR << "Failed to add RecallService";
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

    if (server.Start(FLAGS_server_port, &server_options) != 0) {
        LOG_ERROR << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }

    LOG_INFO << "RecallService started on port " << FLAGS_server_port;

    server.RunUntilAskedToQuit();

    LOG_INFO << "Recall Service stopped";
    return 0;
}
