#include "brpc_registry_backend.h"

#include <brpc/controller.h>

#include "common/logger.h"
#include "discovery.pb.h"

namespace discovery {

BrpcRegistryBackend::BrpcRegistryBackend(const std::string& discovery_addr) {
    brpc::ChannelOptions opts;
    opts.timeout_ms = 5000;
    opts.max_retry = 2;
    if (channel_.Init(discovery_addr.c_str(), &opts) != 0) {
        LOG_ERROR << "Failed to connect to discovery server at "
                   << discovery_addr;
    }
}

RegisterResult BrpcRegistryBackend::Register(
    const std::string& service_name,
    const std::string& host,
    int32_t port,
    int32_t heartbeat_interval_sec) {

    RegisterResult result;

    discovery::DiscoveryService_Stub stub(&channel_);

    discovery::RegisterRequest req;
    discovery::RegisterResponse rsp;
    brpc::Controller cntl;

    req.mutable_instance()->set_service_name(service_name);
    req.mutable_instance()->set_host(host);
    req.mutable_instance()->set_port(port);
    req.set_heartbeat_interval_sec(heartbeat_interval_sec);

    stub.Register(&cntl, &req, &rsp, nullptr);
    if (cntl.Failed()) {
        LOG_ERROR << "Register failed: " << cntl.ErrorText();
        return result;
    }

    result.success = rsp.success();
    result.instance_id = rsp.instance_id();
    result.heartbeat_interval_sec = rsp.heartbeat_interval_sec();
    return result;
}

bool BrpcRegistryBackend::Deregister(
    const std::string& service_name,
    const std::string& instance_id) {

    discovery::DiscoveryService_Stub stub(&channel_);

    discovery::DeregisterRequest req;
    discovery::DeregisterResponse rsp;
    brpc::Controller cntl;

    req.set_service_name(service_name);
    req.set_instance_id(instance_id);

    stub.Deregister(&cntl, &req, &rsp, nullptr);
    if (cntl.Failed()) {
        LOG_ERROR << "Deregister failed: " << cntl.ErrorText();
        return false;
    }

    return rsp.success();
}

RegisterResult BrpcRegistryBackend::Heartbeat(
    const std::string& service_name,
    const std::string& instance_id) {

    RegisterResult result;
    result.success = true;

    discovery::DiscoveryService_Stub stub(&channel_);

    discovery::HeartbeatRequest req;
    discovery::HeartbeatResponse rsp;
    brpc::Controller cntl;

    req.set_service_name(service_name);
    req.set_instance_id(instance_id);

    stub.Heartbeat(&cntl, &req, &rsp, nullptr);
    if (cntl.Failed()) {
        LOG_ERROR << "Heartbeat failed: " << cntl.ErrorText();
        result.success = false;
        return result;
    }

    if (rsp.needs_reregister()) {
        LOG_WARN << "Server lost state, re-registering";
        result.needs_reregister = true;
    }

    return result;
}

} // namespace discovery
