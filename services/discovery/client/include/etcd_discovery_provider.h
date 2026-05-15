#ifndef ETCD_DISCOVERY_PROVIDER_H
#define ETCD_DISCOVERY_PROVIDER_H

#include <memory>
#include <string>
#include <vector>

#include "discovery.pb.h"
#include "discovery_provider.h"
#include "etcd_client.h"

namespace discovery {

class EtcdDiscoveryProvider : public IDiscoveryProvider {
public:
    explicit EtcdDiscoveryProvider(const std::string& etcd_endpoints);

    std::vector<ServiceInstance> Discover(
        const std::string& service_name) override;

private:
    std::unique_ptr<EtcdClient> client_;
};

} // namespace discovery

#endif // ETCD_DISCOVERY_PROVIDER_H
