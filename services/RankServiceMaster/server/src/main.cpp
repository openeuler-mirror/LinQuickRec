#include "rank_master_server.h"
#include "common/global_thread_pool.h"
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    rank::RankMasterServiceImpl precalc_service;
    
    brpc::Server server;
    
    if (server.AddService(&precalc_service, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add RankMasterService";
        return -1;
    }
    
    brpc::ServerOptions server_options;
    server_options.num_threads = 128;
    
    if (server.Start(FLAGS_server_port, &server_options) != 0) {
        LOG(ERROR) << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }
    
    LOG(INFO) << "RankMasterService started on port " << FLAGS_server_port;
    LOG(INFO) << "Sub-worker Count: " << FLAGS_sub_worker_count;
    LOG(INFO) << "Sub-worker Addresses: " << FLAGS_sub_worker_addresses;
    LOG(INFO) << "Top-K: " << FLAGS_top_k;
    LOG(INFO) << "Thread Pool Size: " << FLAGS_global_thread_pool_size;
    LOG(INFO) << "Timing Stats: " << (FLAGS_enable_timing_stats ? "enabled" : "disabled");
    
    server.RunUntilAskedToQuit();
    
    LOG(INFO) << "RankMasterService stopped";
    return 0;
}
