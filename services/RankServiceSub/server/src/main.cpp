#include "rank_sub_server.h"
#include "common/global_thread_pool.h"
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    rank::RankSubServiceImpl precalc_service;
    
    brpc::Server server;
    
    if (server.AddService(&precalc_service, brpc::SERVER_OWNS_SERVICE) != 0) {
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
