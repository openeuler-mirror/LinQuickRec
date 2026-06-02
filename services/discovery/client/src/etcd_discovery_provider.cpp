#include "etcd_discovery_provider.h"

#include <rapidjson/document.h>

#include "common/logger.h"

using namespace rapidjson;

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
        Document d;
        d.Parse(value.c_str());
        if (d.HasParseError()) {
            LOG_ERROR << "EtcdDiscoveryProvider parse error for key " << key;
            continue;
        }

        ServiceInstance inst;
        inst.set_service_name(service_name);
        inst.set_host(d["host"].GetString());
        inst.set_port(d["port"].GetInt());

        size_t last_slash = key.rfind('/');
        if (last_slash != std::string::npos) {
            inst.set_instance_id(key.substr(last_slash + 1));
        }

        inst.set_status(InstanceStatus::UP);
        result.push_back(std::move(inst));
    }

    return result;
}

} // namespace discovery
