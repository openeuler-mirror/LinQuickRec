#include "common/logger.h"
#include "discovery.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <string>

DEFINE_string(server, "127.0.0.1:8100", "Discovery server address");

int main(int argc, char* argv[]) {
    {
        common::logger::LoggerConfig cfg;
        cfg.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%f:%L] %v";
        common::logger::Initialize(cfg);
    }
    google::ParseCommandLineFlags(&argc, &argv, true);

    if (argc < 2) {
        LOG_ERROR << "Usage: test_discover --server=<addr> <service_type>";
        return 1;
    }

    std::string service_type = argv[1];

    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.timeout_ms = 5000;
    opts.max_retry = 1;
    if (channel.Init(FLAGS_server.c_str(), &opts) != 0) {
        LOG_ERROR << "Failed to connect to " << FLAGS_server;
        return 1;
    }

    discovery::DiscoveryService_Stub stub(&channel);
    discovery::DiscoverRequest req;
    discovery::DiscoverResponse rsp;
    brpc::Controller cntl;

    req.set_service_name(service_type);
    stub.Discover(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed()) {
        LOG_ERROR << "Discover RPC failed: " << cntl.ErrorText();
        return 1;
    }

    int n = rsp.instances_size();
    LOG_INFO << "[PASS] Found " << n << " instance(s) of [" << service_type << "]:";
    for (int i = 0; i < n; ++i) {
        const auto& inst = rsp.instances(i);
        std::string status;
        if (inst.status() == discovery::InstanceStatus::UP) status = "UP";
        else if (inst.status() == discovery::InstanceStatus::DOWN) status = "DOWN";
        else status = std::to_string(inst.status());
        LOG_INFO << "  [" << i << "] " << inst.instance_id()
                 << "  " << inst.host() << ":" << inst.port()
                 << "  status=" << status;
    }

    return 0;
}
