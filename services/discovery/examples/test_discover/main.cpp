#include "discovery.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>
#include <butil/logging.h>

#include <iostream>
#include <string>

DEFINE_string(server, "127.0.0.1:8100", "Discovery server address");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    if (argc < 2) {
        std::cerr << "Usage: test_discover --server=<addr> <service_type>" << std::endl;
        return 1;
    }

    std::string service_type = argv[1];

    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.timeout_ms = 5000;
    opts.max_retry = 1;
    if (channel.Init(FLAGS_server.c_str(), &opts) != 0) {
        std::cerr << "Failed to connect to " << FLAGS_server << std::endl;
        return 1;
    }

    discovery::DiscoveryService_Stub stub(&channel);
    discovery::DiscoverRequest req;
    discovery::DiscoverResponse rsp;
    brpc::Controller cntl;

    req.set_service_name(service_type);
    stub.Discover(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed()) {
        std::cerr << "Discover RPC failed: " << cntl.ErrorText() << std::endl;
        return 1;
    }

    int n = rsp.instances_size();
    std::cout << "Found " << n << " instance(s) of [" << service_type << "]:" << std::endl;
    for (int i = 0; i < n; ++i) {
        const auto& inst = rsp.instances(i);
        std::cout << "  [" << i << "] " << inst.instance_id()
                  << "  " << inst.host() << ":" << inst.port()
                  << "  status=";
        if (inst.status() == InstanceStatus::UP) std::cout << "UP";
        else if (inst.status() == InstanceStatus::DOWN) std::cout << "DOWN";
        else std::cout << inst.status();
        std::cout << std::endl;
    }

    return 0;
}
