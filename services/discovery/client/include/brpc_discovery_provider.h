#ifndef BRPC_DISCOVERY_PROVIDER_H
#define BRPC_DISCOVERY_PROVIDER_H

#include <memory>
#include <string>
#include <vector>

#include <brpc/channel.h>

#include "discovery.pb.h"
#include "discovery_provider.h"

namespace discovery {

class BrpcDiscoveryProvider : public IDiscoveryProvider {
public:
    explicit BrpcDiscoveryProvider(const std::string& discovery_addr);

    std::vector<ServiceInstance> Discover(
        const std::string& service_name) override;

private:
    brpc::Channel channel_;
};

} // namespace discovery

#endif // BRPC_DISCOVERY_PROVIDER_H
