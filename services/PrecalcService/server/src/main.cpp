#include "precalc_server.h"
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    logging::SetLoggingLevel(logging::BLOG_INFO);
    butil::AtExitManager exit_manager;
    
    precalc::PrecalcServiceImpl precalc_service;
    
    brpc::Server server;
    
    if (server.AddService(&precalc_service, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add PrecalcService";
        return -1;
    }
    
    brpc::ServerOptions server_options;
    server_options.num_threads = 128;
    
    if (server.Start(FLAGS_server_port, &server_options) != 0) {
        LOG(ERROR) << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }
    
    LOG(INFO) << "PrecalcService started on port " << FLAGS_server_port;
    LOG(INFO) << "Precalc result size: " << FLAGS_precalc_result_size_mb << " MB";
    LOG(INFO) << "Response total size: " << FLAGS_response_total_size_kb << " KB (key + payload)";
    LOG(INFO) << "TTL: " << FLAGS_ttl_seconds << " seconds";
    LOG(INFO) << "KVWorker address: " << FLAGS_kvworker_host << ":" << FLAGS_kvworker_port;
    
    server.RunUntilAskedToQuit();
    
    LOG(INFO) << "PrecalcService stopped";
    return 0;
}
