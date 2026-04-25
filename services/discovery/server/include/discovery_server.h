#ifndef DISCOVERY_SERVER_H
#define DISCOVERY_SERVER_H

#include "discovery.pb.h"

#include <brpc/server.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <cstdint>

DECLARE_int32(server_port);
DECLARE_int32(heartbeat_check_interval_ms);
DECLARE_double(heartbeat_grace_factor);
DECLARE_double(cleanup_factor);
DECLARE_int32(default_heartbeat_interval_sec);

namespace discovery {

class DiscoveryServiceImpl : public DiscoveryService {
public:
    DiscoveryServiceImpl();
    ~DiscoveryServiceImpl();

    void Register(google::protobuf::RpcController* controller,
                  const RegisterRequest* request,
                  RegisterResponse* response,
                  google::protobuf::Closure* done) override;

    void Deregister(google::protobuf::RpcController* controller,
                    const DeregisterRequest* request,
                    DeregisterResponse* response,
                    google::protobuf::Closure* done) override;

    void Heartbeat(google::protobuf::RpcController* controller,
                   const HeartbeatRequest* request,
                   HeartbeatResponse* response,
                   google::protobuf::Closure* done) override;

    void Discover(google::protobuf::RpcController* controller,
                  const DiscoverRequest* request,
                  DiscoverResponse* response,
                  google::protobuf::Closure* done) override;

private:
    struct InstanceEntry {
        std::string instance_id;
        std::string service_name;
        std::string host;
        int32_t port;
        std::map<std::string, std::string> metadata;
        InstanceStatus status;
        int64_t last_heartbeat_us;
        int32_t heartbeat_interval_sec;

        ServiceInstance to_proto() const {
            ServiceInstance inst;
            inst.set_service_name(service_name);
            inst.set_host(host);
            inst.set_port(port);
            inst.set_instance_id(instance_id);
            for (const auto& [k, v] : metadata) {
                (*inst.mutable_metadata())[k] = v;
            }
            inst.set_status(status);
            inst.set_last_heartbeat(last_heartbeat_us);
            return inst;
        }
    };

    void health_check_loop();
    std::string generate_instance_id(const std::string& service_name,
                                     const std::string& host,
                                     int32_t port);

    std::map<std::string, std::map<std::string, InstanceEntry>> registry_;
    std::mutex mutex_;
    std::thread health_thread_;
    std::atomic<bool> running_{true};
    std::atomic<int64_t> instance_counter_{0};
};

} // namespace discovery

#endif // DISCOVERY_SERVER_H
