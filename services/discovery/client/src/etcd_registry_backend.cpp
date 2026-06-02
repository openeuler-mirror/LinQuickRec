#include "etcd_registry_backend.h"

#include <chrono>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "common/logger.h"

using namespace rapidjson;

namespace discovery {

namespace {

const char* KEY_PREFIX = "/linquickrec/services/";

std::string makeKey(const std::string& service_name,
                     const std::string& instance_id) {
    return std::string(KEY_PREFIX) + service_name + "/" + instance_id;
}

std::string makeInstanceId(const std::string& service_name,
                            const std::string& host,
                            int32_t port) {
    return service_name + "_" + host + "_" + std::to_string(port);
}

} // namespace

EtcdRegistryBackend::EtcdRegistryBackend(const std::string& etcd_endpoints) {
    client_ = std::make_unique<EtcdClient>(etcd_endpoints);
    LOG_INFO << "EtcdRegistryBackend connected to " << etcd_endpoints;
}

EtcdRegistryBackend::~EtcdRegistryBackend() {
    running_ = false;
    if (keep_alive_thread_.joinable()) {
        keep_alive_thread_.join();
    }
    if (lease_id_ > 0) {
        client_->leaseRevoke(lease_id_);
    }
}

RegisterResult EtcdRegistryBackend::Register(
    const std::string& service_name,
    const std::string& host,
    int32_t port,
    int32_t heartbeat_interval_sec) {

    RegisterResult result;

    std::string instance_id = makeInstanceId(service_name, host, port);
    std::string key = makeKey(service_name, instance_id);

    int64_t ttl = static_cast<int64_t>(heartbeat_interval_sec) * 3;
    int64_t lease_id = client_->leaseGrant(ttl);
    if (lease_id <= 0) {
        LOG_ERROR << "EtcdRegistryBackend: leaseGrant failed";
        return result;
    }

    Document val;
    val.SetObject();
    auto& alloc = val.GetAllocator();
    val.AddMember("host", Value(host.c_str(), alloc), alloc);
    val.AddMember("port", port, alloc);

    StringBuffer buf;
    Writer<StringBuffer> w(buf);
    val.Accept(w);

    if (!client_->put(key, buf.GetString(), lease_id)) {
        LOG_ERROR << "EtcdRegistryBackend: put failed";
        client_->leaseRevoke(lease_id);
        return result;
    }

    lease_id_ = lease_id;
    registered_key_ = key;
    running_ = true;
    keep_alive_thread_ = std::thread(
        &EtcdRegistryBackend::keepAliveLoop, this, lease_id, heartbeat_interval_sec);

    result.success = true;
    result.instance_id = instance_id;
    result.heartbeat_interval_sec = heartbeat_interval_sec;

    LOG_INFO << "Etcd registered: " << service_name << " [" << instance_id
              << "] " << host << ":" << port << " (lease=" << lease_id
              << ", ttl=" << ttl << "s)";
    return result;
}

bool EtcdRegistryBackend::Deregister(
    const std::string& service_name,
    const std::string& instance_id) {

    running_ = false;
    if (keep_alive_thread_.joinable()) {
        keep_alive_thread_.join();
    }

    if (!registered_key_.empty()) {
        client_->deleteKey(registered_key_);
    }
    if (lease_id_ > 0) {
        client_->leaseRevoke(lease_id_);
        lease_id_ = 0;
    }
    registered_key_.clear();

    LOG_INFO << "Etcd deregistered: " << service_name
              << " [" << instance_id << "]";
    return true;
}

RegisterResult EtcdRegistryBackend::Heartbeat(
    const std::string& /*service_name*/,
    const std::string& /*instance_id*/) {
    RegisterResult result;
    result.success = true;
    return result;
}

void EtcdRegistryBackend::keepAliveLoop(int64_t lease_id,
                                         int32_t interval_sec) {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::seconds(interval_sec));
        if (!running_) break;

        Document d;
        d.SetObject();
        auto& alloc = d.GetAllocator();
        d.AddMember("ID", Value(std::to_string(lease_id).c_str(), alloc), alloc);

        StringBuffer buf;
        Writer<StringBuffer> w(buf);
        d.Accept(w);

        std::string resp = client_->post("/v3/lease/keepalive", buf.GetString());
        if (resp.empty()) {
            LOG_WARN << "Etcd keep-alive failed for lease " << lease_id;
        } else {
            Document r;
            r.Parse(resp.c_str());
            if (!r.HasParseError() && r.HasMember("error")) {
                LOG_ERROR << "Etcd keep-alive error for lease " << lease_id
                          << ": " << r["error"].GetString();
            }
        }
    }
}

} // namespace discovery
