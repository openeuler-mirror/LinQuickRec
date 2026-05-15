#ifndef BRPC_REGISTRY_BACKEND_H
#define BRPC_REGISTRY_BACKEND_H

#include <memory>
#include <string>

#include <brpc/channel.h>

#include "registry_backend.h"

namespace discovery {

class BrpcRegistryBackend : public IRegistryBackend {
public:
    explicit BrpcRegistryBackend(const std::string& discovery_addr);

    RegisterResult Register(const std::string& service_name,
                            const std::string& host,
                            int32_t port,
                            int32_t heartbeat_interval_sec) override;

    bool Deregister(const std::string& service_name,
                    const std::string& instance_id) override;

    RegisterResult Heartbeat(const std::string& service_name,
                             const std::string& instance_id) override;

private:
    brpc::Channel channel_;
};

} // namespace discovery

#endif // BRPC_REGISTRY_BACKEND_H
