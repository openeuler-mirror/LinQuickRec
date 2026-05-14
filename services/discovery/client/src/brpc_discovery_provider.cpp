#include "brpc_discovery_provider.h"

#include <brpc/controller.h>

#include "common/logger.h"
#include "discovery.pb.h"

namespace discovery {

BrpcDiscoveryProvider::BrpcDiscoveryProvider(
    const std::string& discovery_addr) {
    brpc::ChannelOptions opts;
    opts.timeout_ms = 3000;
    opts.max_retry = 2;
    if (channel_.Init(discovery_addr.c_str(), &opts) != 0) {
        LOG_ERROR << "Failed to init discovery channel to " << discovery_addr;
    }
}

std::vector<ServiceInstance> BrpcDiscoveryProvider::Discover(
    const std::string& service_name) {

    discovery::DiscoverRequest req;
    req.set_service_name(service_name);

    discovery::DiscoverResponse rsp;
    brpc::Controller cntl;

    discovery::DiscoveryService_Stub stub(&channel_);
    stub.Discover(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed()) {
        LOG_ERROR << "Discover(" << service_name
                   << ") failed: " << cntl.ErrorText();
        return {};
    }

    std::vector<ServiceInstance> result;
    for (const auto& inst : rsp.instances()) {
        if (inst.status() == InstanceStatus::UP) {
            result.push_back(inst);
        }
    }
    return result;
}

} // namespace discovery
