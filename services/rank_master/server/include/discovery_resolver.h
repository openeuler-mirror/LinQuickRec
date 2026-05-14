#ifndef DISCOVERY_RESOLVER_H
#define DISCOVERY_RESOLVER_H

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "discovery.pb.h"
#include "discovery_provider.h"

class DiscoveryResolver {
public:
    struct Instance {
        std::string host;
        int32_t port;
        std::string instance_id;
    };

    explicit DiscoveryResolver(const std::string& backend_type,
                               const std::string& address);

    std::vector<Instance> discover(const std::string& service_name);

    std::string select_one(const std::string& service_name);

    void refresh(const std::string& service_name);

private:
    void refresh_unlocked(const std::string& service_name);

    std::unique_ptr<discovery::IDiscoveryProvider> provider_;
    std::string addr_;
    std::mutex mutex_;
    std::map<std::string, std::vector<Instance>> cache_;
    std::atomic<size_t> rr_counter_{0};
};

#endif // DISCOVERY_RESOLVER_H
