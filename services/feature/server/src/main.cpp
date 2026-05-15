#include "feature_server.h"

#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/logger.h"

DEFINE_int32(server_num_threads, 0,
             "Server bthread num_threads, 0 = BRPC default");
DEFINE_int32(server_idle_timeout_sec, -1,
             "Server idle connection timeout (sec), -1 = BRPC default");
DEFINE_int32(server_max_concurrency, 0,
             "Server max concurrency, 0 = no limit");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    LOG_INFO << "Feature Service starting...";

    feature::FeatureServiceImpl service_impl;

    brpc::Server server;

    if (server.AddService(&service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR << "Failed to add FeatureService";
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
    LOG_INFO << "Feature Service Started";
    LOG_INFO << "===========================================";
    LOG_INFO << "Listening on: " << server_addr;
    LOG_INFO << "===========================================";

    server.RunUntilAskedToQuit();

    LOG_INFO << "Feature Service stopped";
    return 0;
}
