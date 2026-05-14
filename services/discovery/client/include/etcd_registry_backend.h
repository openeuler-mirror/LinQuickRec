#ifndef ETCD_REGISTRY_BACKEND_H
#define ETCD_REGISTRY_BACKEND_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "etcd_client.h"
#include "registry_backend.h"

namespace discovery {

class EtcdRegistryBackend : public IRegistryBackend {
public:
    explicit EtcdRegistryBackend(const std::string& etcd_endpoints);
    ~EtcdRegistryBackend();

    RegisterResult Register(const std::string& service_name,
                            const std::string& host,
                            int32_t port,
                            int32_t heartbeat_interval_sec) override;

    bool Deregister(const std::string& service_name,
                    const std::string& instance_id) override;

    RegisterResult Heartbeat(const std::string& service_name,
                             const std::string& instance_id) override;

private:
    void keepAliveLoop(int64_t lease_id, int32_t interval_sec);

    std::unique_ptr<EtcdClient> client_;

    std::thread keep_alive_thread_;
    std::atomic<bool> running_{false};
    int64_t lease_id_ = 0;
    std::string registered_key_;
};

} // namespace discovery

#endif // ETCD_REGISTRY_BACKEND_H
