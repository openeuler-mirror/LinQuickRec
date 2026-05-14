#include "discovery_resolver.h"

#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"

DiscoveryResolver::DiscoveryResolver(const std::string& backend_type,
                                     const std::string& address)
    : addr_(address) {
    provider_ = discovery::CreateDiscoveryProvider(backend_type, address);
}

std::vector<DiscoveryResolver::Instance>
DiscoveryResolver::discover(const std::string& service_name) {
    auto instances = provider_->Discover(service_name);

    std::vector<Instance> result;
    for (const auto& inst : instances) {
        result.push_back({inst.host(), inst.port(), inst.instance_id()});
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
