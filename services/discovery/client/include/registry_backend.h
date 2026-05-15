#ifndef DISCOVERY_REGISTRY_BACKEND_H
#define DISCOVERY_REGISTRY_BACKEND_H

#include <cstdint>
#include <memory>
#include <string>

namespace discovery {

struct RegisterResult {
    bool success = false;
    std::string instance_id;
    int32_t heartbeat_interval_sec = 5;
    bool needs_reregister = false;
};

class IRegistryBackend {
public:
    virtual ~IRegistryBackend() = default;

    virtual RegisterResult Register(const std::string& service_name,
                                    const std::string& host,
                                    int32_t port,
                                    int32_t heartbeat_interval_sec) = 0;

    virtual bool Deregister(const std::string& service_name,
                            const std::string& instance_id) = 0;

    virtual RegisterResult Heartbeat(const std::string& service_name,
                                     const std::string& instance_id) = 0;
};

std::unique_ptr<IRegistryBackend> CreateRegistryBackend(
    const std::string& backend_type,
    const std::string& address);

} // namespace discovery

#endif // DISCOVERY_REGISTRY_BACKEND_H
