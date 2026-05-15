#include "etcd_discovery_provider.h"

#include "common/logger.h"
#include "simple_json.h"

namespace discovery {

namespace {

const char* KEY_PREFIX = "/linquickrec/services/";

} // namespace

EtcdDiscoveryProvider::EtcdDiscoveryProvider(
    const std::string& etcd_endpoints) {
    client_ = std::make_unique<EtcdClient>(etcd_endpoints);
    LOG_INFO << "EtcdDiscoveryProvider connected to " << etcd_endpoints;
}

std::vector<ServiceInstance> EtcdDiscoveryProvider::Discover(
    const std::string& service_name) {

    std::string prefix = std::string(KEY_PREFIX) + service_name + "/";
    auto kvs = client_->range(prefix);

    std::vector<ServiceInstance> result;
    for (const auto& [key, value] : kvs) {
        try {
            simple_json::Value val = simple_json::Value::parse(value);
            ServiceInstance inst;
            inst.set_service_name(service_name);
            inst.set_host(val.get("host").str());
            inst.set_port(static_cast<int32_t>(val.get("port").integer()));

            size_t last_slash = key.rfind('/');
            if (last_slash != std::string::npos) {
                inst.set_instance_id(key.substr(last_slash + 1));
            }

            inst.set_status(InstanceStatus::UP);
            result.push_back(std::move(inst));
        } catch (const std::exception& e) {
            LOG_ERROR << "EtcdDiscoveryProvider parse error for key "
                       << key << ": " << e.what();
        }
    }

    return result;
}

} // namespace discovery
