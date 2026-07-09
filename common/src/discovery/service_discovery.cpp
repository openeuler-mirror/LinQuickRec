#include "common/service_discovery.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/logger.h"
#include "discovery.pb.h"
#include "discovery_provider.h"

namespace common {

static constexpr int COOLDOWN_SECONDS = 10;
static constexpr int MAX_CONSECUTIVE_FAILURES = 3;

struct ServiceDiscovery::Impl {
    struct InstanceState {
        int consecutive_failures = 0;
        std::chrono::steady_clock::time_point cooldown_until;

        bool in_cooldown() const {
            return std::chrono::steady_clock::now() < cooldown_until;
        }
    };

    std::unique_ptr<discovery::IDiscoveryProvider> provider;
    std::unordered_map<std::string, std::vector<discovery::ServiceInstance>> cache;
    std::unordered_map<std::string, size_t> rr_index;
    std::unordered_map<std::string, InstanceState> instance_states;
    std::unordered_map<std::string, bool> first_refresh;

    int refresh_interval_ms;
    std::thread refresh_thread;
    std::atomic<bool> running{true};
    mutable std::mutex mutex;
};

ServiceDiscovery::ServiceDiscovery(const std::string& backend_type,
                                   const std::string& address,
                                   int refresh_interval_ms)
    : impl_(new Impl()) {
    impl_->refresh_interval_ms = refresh_interval_ms;
    impl_->provider = discovery::CreateDiscoveryProvider(backend_type, address);
    LOG_INFO << "ServiceDiscovery connected to " << address
              << " (backend: " << backend_type << ")";

    impl_->refresh_thread = std::thread(&ServiceDiscovery::refresh_loop, this);
}

ServiceDiscovery::~ServiceDiscovery() {
    impl_->running = false;
    if (impl_->refresh_thread.joinable()) {
        impl_->refresh_thread.join();
    }
}

void ServiceDiscovery::refresh_loop() {
    while (impl_->running) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(impl_->refresh_interval_ms));
        if (impl_->running) {
            refresh_all();
        }
    }
}

void ServiceDiscovery::refresh_all() {
    if (!impl_->provider) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);

    for (auto& [svc, _] : impl_->cache) {
        auto instances = impl_->provider->Discover(svc);

        int old_count = static_cast<int>(impl_->cache[svc].size());
        int new_count = static_cast<int>(instances.size());

        impl_->cache[svc] = std::move(instances);
        if (impl_->rr_index.find(svc) == impl_->rr_index.end()) {
            impl_->rr_index[svc] = 0;
        }

        bool is_first = !impl_->first_refresh[svc];
        impl_->first_refresh[svc] = true;

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
    std::lock_guard<std::mutex> lock(impl_->mutex);

    auto it = impl_->cache.find(service_name);

    if (it == impl_->cache.end()) {
        if (!impl_->provider) return false;

        auto instances = impl_->provider->Discover(service_name);
        int count = static_cast<int>(instances.size());
        impl_->cache[service_name] = std::move(instances);
        impl_->rr_index[service_name] = 0;
        impl_->first_refresh[service_name] = true;

        LOG_INFO << "Discover(" << service_name << "): "
                 << count << " instance(s) (lazy init)";

        it = impl_->cache.find(service_name);
        if (it == impl_->cache.end() || it->second.empty()) {
            LOG_WARN << "Discover(" << service_name
                     << "): provider returned 0 instances (lazy init)";
            return false;
        }
    }

    if (it->second.empty()) return false;

    auto& instances = it->second;
    auto& idx = impl_->rr_index[service_name];

    for (size_t i = 0; i < instances.size(); ++i) {
        size_t candidate = (idx + i) % instances.size();
        const auto& inst = instances[candidate];
        auto state_it = impl_->instance_states.find(inst.instance_id());

        if (state_it != impl_->instance_states.end() && state_it->second.in_cooldown()) {
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
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->instance_states.erase(instance_id);
}

void ServiceDiscovery::ReportFailure(const std::string& instance_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& state = impl_->instance_states[instance_id];
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
