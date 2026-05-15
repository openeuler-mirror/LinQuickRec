#ifndef DISCOVERY_PROVIDER_H
#define DISCOVERY_PROVIDER_H

#include <memory>
#include <string>
#include <vector>

#include "discovery.pb.h"

namespace discovery {

class IDiscoveryProvider {
public:
    virtual ~IDiscoveryProvider() = default;

    virtual std::vector<ServiceInstance> Discover(
        const std::string& service_name) = 0;
};

std::unique_ptr<IDiscoveryProvider> CreateDiscoveryProvider(
    const std::string& backend_type,
    const std::string& address);

} // namespace discovery

#endif // DISCOVERY_PROVIDER_H
