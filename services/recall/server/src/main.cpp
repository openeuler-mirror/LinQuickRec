#include "recall_server.h"

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/logger.h"
#include "common/perf_handler.h"

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
    log_config.file_path = "/var/log/linquickrec/recall.log";
    log_config.max_file_size = 100 * 1024 * 1024;
    log_config.max_files = 5;
    log_config.enable_trace_id = true;
    common::logger::Initialize(log_config);
    common::perf::PerfRingRegistry::Instance().Init();

    recall::RecallServiceImpl service_impl;

    if (!service_impl.IsReady()) {
        LOG_ERROR << "RecallServiceImpl initialization failed, shutting down";
        return -1;
    }

    brpc::Server server;

    if (server.AddService(&service_impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR << "Failed to add RecallService";
        return -1;
    }

    if (server.AddService(new common::perf::PerfService,
                          brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG_ERROR << "Failed to add PerfService";
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

    LOG_INFO << "RecallService started on port " << FLAGS_server_port;

    server.RunUntilAskedToQuit();

    LOG_INFO << "Recall Service stopped";
    return 0;
}
