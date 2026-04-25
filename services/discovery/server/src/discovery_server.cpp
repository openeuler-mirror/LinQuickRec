#include "discovery_server.h"

#include <chrono>
#include <sstream>
#include <cstdint>

DEFINE_int32(server_port, 8100, "Discovery server listening port");
DEFINE_int32(heartbeat_check_interval_ms, 1000, "Health check scan interval (ms)");
DEFINE_double(heartbeat_grace_factor, 2.0, "Heartbeat timeout multiplier");
DEFINE_double(cleanup_factor, 5.0, "Stale instance cleanup multiplier");
DEFINE_int32(default_heartbeat_interval_sec, 5, "Default heartbeat interval (seconds)");

namespace discovery {

DiscoveryServiceImpl::DiscoveryServiceImpl() {
    LOG(INFO) << "DiscoveryServiceImpl initializing...";
    health_thread_ = std::thread(&DiscoveryServiceImpl::health_check_loop, this);
    LOG(INFO) << "DiscoveryServiceImpl initialized, health check thread started";
}

DiscoveryServiceImpl::~DiscoveryServiceImpl() {
    running_ = false;
    if (health_thread_.joinable()) {
        health_thread_.join();
    }
    LOG(INFO) << "DiscoveryServiceImpl destroyed";
}

std::string DiscoveryServiceImpl::generate_instance_id(
    const std::string& service_name,
    const std::string& host,
    int32_t port) {
    int64_t id = ++instance_counter_;
    std::ostringstream oss;
    oss << service_name << "_" << host << "_" << port << "_" << id;
    return oss.str();
}

void DiscoveryServiceImpl::Register(
    google::protobuf::RpcController* controller,
    const RegisterRequest* request,
    RegisterResponse* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);

    const auto& inst = request->instance();
    std::string instance_id = generate_instance_id(
        inst.service_name(), inst.host(), inst.port());

    int32_t interval = request->heartbeat_interval_sec();
    if (interval <= 0) {
        interval = FLAGS_default_heartbeat_interval_sec;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        InstanceEntry entry;
        entry.instance_id = instance_id;
        entry.service_name = inst.service_name();
        entry.host = inst.host();
        entry.port = inst.port();
        for (const auto& [k, v] : inst.metadata()) {
            entry.metadata[k] = v;
        }
        entry.status = InstanceStatus::UP;
        entry.last_heartbeat_us = butil::gettimeofday_us();
        entry.heartbeat_interval_sec = interval;

        registry_[inst.service_name()][instance_id] = std::move(entry);
    }

    response->set_success(true);
    response->set_instance_id(instance_id);
    response->set_heartbeat_interval_sec(interval);
    response->set_message("registered successfully");

    LOG(INFO) << "Registered: " << inst.service_name()
              << " [" << instance_id << "] "
              << inst.host() << ":" << inst.port();
}

void DiscoveryServiceImpl::Deregister(
    google::protobuf::RpcController* controller,
    const DeregisterRequest* request,
    DeregisterResponse* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto svc_it = registry_.find(request->service_name());
        if (svc_it != registry_.end()) {
            svc_it->second.erase(request->instance_id());
            if (svc_it->second.empty()) {
                registry_.erase(svc_it);
            }
        }
    }

    response->set_success(true);
    response->set_message("deregistered successfully");

    LOG(INFO) << "Deregistered: " << request->service_name()
              << " [" << request->instance_id() << "]";
}

void DiscoveryServiceImpl::Heartbeat(
    google::protobuf::RpcController* controller,
    const HeartbeatRequest* request,
    HeartbeatResponse* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);

    int64_t now = butil::gettimeofday_us();
    bool needs_reregister = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto svc_it = registry_.find(request->service_name());
        if (svc_it != registry_.end()) {
            auto inst_it = svc_it->second.find(request->instance_id());
            if (inst_it != svc_it->second.end()) {
                inst_it->second.last_heartbeat_us = now;
                if (inst_it->second.status == InstanceStatus::DOWN) {
                    inst_it->second.status = InstanceStatus::UP;
                    LOG(INFO) << "Instance recovered: " << request->service_name()
                              << " [" << request->instance_id() << "]";
                }
            } else {
                needs_reregister = true;
            }
        } else {
            needs_reregister = true;
        }
    }

    response->set_success(!needs_reregister);
    response->set_needs_reregister(needs_reregister);
}

void DiscoveryServiceImpl::Discover(
    google::protobuf::RpcController* controller,
    const DiscoverRequest* request,
    DiscoverResponse* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);

    std::lock_guard<std::mutex> lock(mutex_);
    auto svc_it = registry_.find(request->service_name());
    if (svc_it != registry_.end()) {
        for (const auto& [id, entry] : svc_it->second) {
            if (entry.status == InstanceStatus::UP) {
                *response->add_instances() = entry.to_proto();
            }
        }
    }
}

void DiscoveryServiceImpl::health_check_loop() {
    while (running_) {
        int64_t now = butil::gettimeofday_us();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto svc_it = registry_.begin(); svc_it != registry_.end(); ) {
                for (auto inst_it = svc_it->second.begin();
                     inst_it != svc_it->second.end(); ) {
                    int64_t interval_us =
                        inst_it->second.heartbeat_interval_sec * 1000000LL;
                    int64_t elapsed = now - inst_it->second.last_heartbeat_us;

                    if (elapsed > static_cast<int64_t>(
                            interval_us * FLAGS_cleanup_factor)) {
                        LOG(WARNING) << "Removing stale instance: "
                                     << inst_it->second.service_name
                                     << " [" << inst_it->second.instance_id << "]";
                        inst_it = svc_it->second.erase(inst_it);
                        continue;
                    } else if (elapsed > static_cast<int64_t>(
                                   interval_us * FLAGS_heartbeat_grace_factor)) {
                        if (inst_it->second.status == InstanceStatus::UP) {
                            inst_it->second.status = InstanceStatus::DOWN;
                            LOG(WARNING) << "Marked DOWN: "
                                         << inst_it->second.service_name
                                         << " [" << inst_it->second.instance_id << "]";
                        }
                    }
                    ++inst_it;
                }
                if (svc_it->second.empty()) {
                    svc_it = registry_.erase(svc_it);
                } else {
                    ++svc_it;
                }
            }
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(FLAGS_heartbeat_check_interval_ms));
    }
}

} // namespace discovery
