// 1. 对应的头文件
#include "precalc_server.h"

// 2. 标准库头文件

// 3. 系统库头文件

// 4. 其他库头文件
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>

// 5. 本项目内其他头文件

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
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
    LOG(INFO) << "user_feat_key size: " << FLAGS_user_feat_key_size_kb << " KB";
    LOG(INFO) << "TTL: " << FLAGS_ttl_seconds << " seconds";
    LOG(INFO) << "KVWorker address: " << FLAGS_kvworker_host << ":" << FLAGS_kvworker_port;
    
    server.RunUntilAskedToQuit();
    
    LOG(INFO) << "PrecalcService stopped";
    return 0;
}
