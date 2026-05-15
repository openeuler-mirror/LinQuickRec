#ifndef COMMON_DISCOVERY_NAMING_SERVICE_H
#define COMMON_DISCOVERY_NAMING_SERVICE_H

#include <memory>
#include <string>
#include <vector>

#include <brpc/channel.h>
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

namespace common {

class DiscoveryCache {
public:
    static DiscoveryCache* instance();

    int QueryDiscovery(const std::string& service_name,
                       std::vector<brpc::ServerNode>* servers);

private:
    DiscoveryCache();
    brpc::Channel discovery_channel_;
    std::unique_ptr<discovery::DiscoveryService_Stub> stub_;
};

class DiscoveryNamingService : public brpc::NamingService {
public:
    int RunNamingService(const char* service_name,
                         brpc::NamingServiceActions* actions) override;
    NamingService* New() const override;
    void Destroy() override;

protected:
    ~DiscoveryNamingService() override = default;
};

} // namespace common

#endif // COMMON_DISCOVERY_NAMING_SERVICE_H
