#include "service_discovery.h"

#include <chrono>
#include <thread>

#include <gflags/gflags.h>

DECLARE_string(feature_service_name);
DECLARE_string(recall_service_name);
DECLARE_string(precalc_service_name);
DECLARE_string(rank_service_name);

#include "common/logger.h"

static constexpr int COOLDOWN_SECONDS = 10;
static constexpr int MAX_CONSECUTIVE_FAILURES = 3;

ServiceDiscovery::ServiceDiscovery(const std::string& discovery_addr,
                                   int refresh_interval_ms)
    : refresh_interval_ms_(refresh_interval_ms) {

    brpc::Channel* ch = new brpc::Channel();
    brpc::ChannelOptions opts;
    opts.timeout_ms = 2000;
    opts.connection_type = "pooled";
    opts.max_retry = 1;

    if (ch->Init(discovery_addr.c_str(), &opts) != 0) {
        LOG_ERROR << "Failed to connect to discovery server at "
                         << discovery_addr;
        delete ch;
        return;
    }

    stub_ = std::make_unique<discovery::DiscoveryService_Stub>(ch);
    LOG_INFO << "ServiceDiscovery connected to " << discovery_addr;

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
    if (!stub_) return;

    std::vector<std::string> known_services = {
        FLAGS_feature_service_name,
        FLAGS_recall_service_name,
        FLAGS_precalc_service_name,
        FLAGS_rank_service_name
    };

    for (const auto& svc : known_services) {
        discovery::DiscoverRequest req;
        req.set_service_name(svc);
        req.set_include_down(false);

        discovery::DiscoverResponse rsp;
        brpc::Controller cntl;
        cntl.set_timeout_ms(2000);

        stub_->Discover(&cntl, &req, &rsp, nullptr);

        if (cntl.Failed()) {
            LOG_WARN << "Discover(" << svc << ") failed: "
                            << cntl.ErrorText();
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            int old_count = static_cast<int>(cache_[svc].size());
            int new_count = rsp.instances_size();

            cache_[svc] = {rsp.instances().begin(), rsp.instances().end()};
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
}

bool ServiceDiscovery::GetInstance(const std::string& service_name,
                                   std::string& host,
                                   int& port,
                                   std::string& instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_.find(service_name);
    if (it == cache_.end() || it->second.empty()) return false;

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
