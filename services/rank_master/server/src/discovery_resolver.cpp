#include "discovery_resolver.h"

#include "common/logger.h"

DiscoveryResolver::DiscoveryResolver(const std::string& discovery_addr)
    : addr_(discovery_addr) {
    brpc::ChannelOptions opts;
    opts.timeout_ms = 3000;
    opts.max_retry = 2;
    if (channel_.Init(discovery_addr.c_str(), &opts) != 0) {
        LOG_ERROR << "Failed to init discovery channel to " << discovery_addr;
    }
}

std::vector<DiscoveryResolver::Instance>
DiscoveryResolver::discover(const std::string& service_name) {
    discovery::DiscoverRequest req;
    req.set_service_name(service_name);

    discovery::DiscoverResponse rsp;
    brpc::Controller cntl;

    discovery::DiscoveryService_Stub stub(&channel_);
    stub.Discover(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed()) {
        LOG_ERROR << "Discover(" << service_name << ") failed: " << cntl.ErrorText();
        return {};
    }

    std::vector<Instance> result;
    for (const auto& inst : rsp.instances()) {
        if (inst.status() == discovery::InstanceStatus::UP) {
            result.push_back({inst.host(), inst.port(), inst.instance_id()});
        }
    }
    return result;
}

std::string DiscoveryResolver::select_one(const std::string& service_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(service_name);
    if (it == cache_.end() || it->second.empty()) {
        refresh_unlocked(service_name);
        it = cache_.find(service_name);
    }
    if (it == cache_.end() || it->second.empty()) return "";

    size_t idx = (rr_counter_++) % it->second.size();
    const auto& inst = it->second[idx];
    return inst.host + ":" + std::to_string(inst.port);
}

void DiscoveryResolver::refresh(const std::string& service_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    refresh_unlocked(service_name);
}

void DiscoveryResolver::refresh_unlocked(const std::string& service_name) {
    cache_[service_name] = discover(service_name);
}
