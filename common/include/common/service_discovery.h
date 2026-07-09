#ifndef COMMON_SERVICE_DISCOVERY_H
#define COMMON_SERVICE_DISCOVERY_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "discovery.pb.h"
#include "discovery_provider.h"

namespace common {

class ServiceDiscovery {
public:
    ServiceDiscovery(const std::string& backend_type,
                     const std::string& address,
                     int refresh_interval_ms);
    ~ServiceDiscovery();

    bool GetInstance(const std::string& service_name,
                     std::string& host,
                     int& port,
                     std::string& instance_id);

    void ReportSuccess(const std::string& instance_id);
    void ReportFailure(const std::string& instance_id);

private:
    void refresh_loop();
    void refresh_all();

    struct InstanceState {
        int consecutive_failures = 0;
        std::chrono::steady_clock::time_point cooldown_until;

        bool in_cooldown() const {
            return std::chrono::steady_clock::now() < cooldown_until;
        }
    };

    std::unique_ptr<discovery::IDiscoveryProvider> provider_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<discovery::ServiceInstance>> cache_;
    std::unordered_map<std::string, size_t> rr_index_;
    std::unordered_map<std::string, InstanceState> instance_states_;
    std::unordered_map<std::string, bool> first_refresh_;

    int refresh_interval_ms_;
    std::thread refresh_thread_;
    std::atomic<bool> running_{true};
};

} // namespace common

#endif // COMMON_SERVICE_DISCOVERY_H
