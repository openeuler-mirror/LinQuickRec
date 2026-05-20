#include "common/service_discovery.h"

#include <chrono>
#include <thread>

#include "common/logger.h"

namespace common {

static constexpr int COOLDOWN_SECONDS = 10;
static constexpr int MAX_CONSECUTIVE_FAILURES = 3;

ServiceDiscovery::ServiceDiscovery(const std::string& backend_type,
                                   const std::string& address,
                                   int refresh_interval_ms)
    : refresh_interval_ms_(refresh_interval_ms) {

    provider_ = discovery::CreateDiscoveryProvider(backend_type, address);
    LOG_INFO << "ServiceDiscovery connected to " << address
              << " (backend: " << backend_type << ")";

    refresh_thread_ = std::thread(&ServiceDiscovery::refresh_loop, this);
}

ServiceDiscovery::~ServiceDiscovery() {
    running_ = false;
    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }
}

void ServiceDiscovery::refresh_loop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(refresh_interval_ms_));
        if (running_) {
            refresh_all();
        }
    }
}

void ServiceDiscovery::refresh_all() {
    // No-op if no services have been queried yet.
    // Subclasses or callers should populate the service list.
    // This base implementation refreshes all cached services.
    if (!provider_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [svc, _] : cache_) {
        auto instances = provider_->Discover(svc);

        int old_count = static_cast<int>(cache_[svc].size());
        int new_count = static_cast<int>(instances.size());

        cache_[svc] = std::move(instances);
        if (rr_index_.find(svc) == rr_index_.end()) {
            rr_index_[svc] = 0;
        }

        bool is_first = !first_refresh_[svc];
        first_refresh_[svc] = true;

        if (is_first) {
            LOG_INFO << "Discover(" << svc << "): "
                     << new_count << " instance(s) (initial)";
        } else if (new_count != old_count) {
            LOG_INFO << "Discover(" << svc << "): "
                     << old_count << " -> " << new_count << " instance(s)";
        } else {
            LOG_DEBUG << "Discover(" << svc << "): "
                      << new_count << " instance(s)";
        }
    }
}

bool ServiceDiscovery::GetInstance(const std::string& service_name,
                                   std::string& host,
                                   int& port,
                                   std::string& instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_.find(service_name);

    // Cache miss: 同步查询一次，填充 cache，后续 refresh_all 可正常刷新
    if (it == cache_.end()) {
        if (!provider_) return false;

        auto instances = provider_->Discover(service_name);
        int count = static_cast<int>(instances.size());
        cache_[service_name] = std::move(instances);
        rr_index_[service_name] = 0;
        first_refresh_[service_name] = true;

        LOG_INFO << "Discover(" << service_name << "): "
                 << count << " instance(s) (lazy init)";

        it = cache_.find(service_name);
        if (it == cache_.end() || it->second.empty()) {
            LOG_WARN << "Discover(" << service_name
                     << "): provider returned 0 instances (lazy init)";
            return false;
        }
    }

    if (it->second.empty()) return false;

    auto& instances = it->second;
    auto& idx = rr_index_[service_name];

    for (size_t i = 0; i < instances.size(); ++i) {
        size_t candidate = (idx + i) % instances.size();
        const auto& inst = instances[candidate];
        auto state_it = instance_states_.find(inst.instance_id());

        if (state_it != instance_states_.end() && state_it->second.in_cooldown()) {
            continue;
        }

        idx = (candidate + 1) % instances.size();
        host = inst.host();
        port = inst.port();
        instance_id = inst.instance_id();
        return true;
    }

    return false;
}

void ServiceDiscovery::ReportSuccess(const std::string& instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    instance_states_.erase(instance_id);
}

void ServiceDiscovery::ReportFailure(const std::string& instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = instance_states_[instance_id];
    state.consecutive_failures++;

    if (state.consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
        state.cooldown_until = std::chrono::steady_clock::now()
                               + std::chrono::seconds(COOLDOWN_SECONDS);
        LOG_WARN << "Circuit breaker opened for instance "
                        << instance_id
                        << " (cooldown " << COOLDOWN_SECONDS << "s)";
    }
}

} // namespace common
