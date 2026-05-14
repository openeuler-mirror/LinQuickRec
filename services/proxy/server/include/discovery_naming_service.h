#ifndef DISCOVERY_NAMING_SERVICE_H
#define DISCOVERY_NAMING_SERVICE_H

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <brpc/naming_service.h>
#include <gflags/gflags.h>

#include "discovery.pb.h"

DECLARE_string(discovery_addr);
DECLARE_int32(discovery_refresh_interval_ms);

DECLARE_int32(discovery_naming_timeout_ms);
DECLARE_string(discovery_naming_connection_type);
DECLARE_int32(discovery_naming_max_retry);
DECLARE_int32(discovery_naming_connect_timeout_ms);
DECLARE_int32(discovery_naming_backup_request_ms);

namespace proxy {

class DiscoveryCache {
public:
    static DiscoveryCache* instance();

    int GetServers(const std::string& service_name,
                   std::vector<brpc::ServerNode>* servers);

private:
    DiscoveryCache();
    ~DiscoveryCache();

    void RefreshLoop();
    int QueryDiscovery(const std::string& service_name,
                       std::vector<brpc::ServerNode>* servers);

    brpc::Channel discovery_channel_;
    std::unique_ptr<discovery::DiscoveryService_Stub> stub_;

    std::mutex mutex_;
    std::map<std::string, std::vector<brpc::ServerNode>> cache_;

    bool running_{true};
    std::thread refresh_thread_;
};

class DiscoveryNamingService : public brpc::NamingService {
public:
    int GetServers(const char* service_name,
                   std::vector<brpc::ServerNode>* servers) override;
    void Describe(std::ostream& os, const brpc::ServerId& id) const override;
};

} // namespace proxy

#endif // DISCOVERY_NAMING_SERVICE_H
