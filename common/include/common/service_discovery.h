#ifndef COMMON_SERVICE_DISCOVERY_H
#define COMMON_SERVICE_DISCOVERY_H

#include <memory>
#include <string>

namespace common {

class ServiceDiscovery {
public:
    ServiceDiscovery(const std::string& backend_type,
                     const std::string& address,
                     int refresh_interval_ms);
    ~ServiceDiscovery();

    bool GetInstance(const std::string& service_name,
                     std::string& host,
                     int& port,
                     std::string& instance_id);

    void ReportSuccess(const std::string& instance_id);
    void ReportFailure(const std::string& instance_id);

private:
    void refresh_loop();
    void refresh_all();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace common

#endif // COMMON_SERVICE_DISCOVERY_H
