#include "discovery_provider.h"
#include "brpc_discovery_provider.h"
#include "etcd_discovery_provider.h"

#include "common/logger.h"

namespace discovery {

std::unique_ptr<IDiscoveryProvider> CreateDiscoveryProvider(
    const std::string& backend_type,
    const std::string& address) {

    if (backend_type == "etcd") {
        return std::make_unique<EtcdDiscoveryProvider>(address);
    }
    if (backend_type != "discovery_server") {
        LOG_WARN << "Unknown discovery provider '" << backend_type
                  << "', falling back to discovery_server";
    }
    return std::make_unique<BrpcDiscoveryProvider>(address);
}

} // namespace discovery
