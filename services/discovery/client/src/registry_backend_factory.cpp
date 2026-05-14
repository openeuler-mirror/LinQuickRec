#include "registry_backend.h"
#include "brpc_registry_backend.h"
#include "etcd_registry_backend.h"

#include "common/logger.h"

namespace discovery {

std::unique_ptr<IRegistryBackend> CreateRegistryBackend(
    const std::string& backend_type,
    const std::string& address) {

    if (backend_type == "etcd") {
        return std::make_unique<EtcdRegistryBackend>(address);
    }
    if (backend_type != "discovery_server") {
        LOG_WARN << "Unknown registry backend '" << backend_type
                  << "', falling back to discovery_server";
    }
    return std::make_unique<BrpcRegistryBackend>(address);
}

} // namespace discovery
