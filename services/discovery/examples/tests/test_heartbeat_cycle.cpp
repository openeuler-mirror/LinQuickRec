#include "discovery.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <string>
#include <thread>
#include <chrono>

#include "common/logger.h"

DEFINE_string(server, "127.0.0.1:8100", "Discovery server address");
DEFINE_string(service_type, "", "Service type (snake_case)");
DEFINE_string(host, "127.0.0.1", "Instance host/IP");
DEFINE_int32(port, 10000, "Instance port");
DEFINE_int32(heartbeat_interval, 3, "Heartbeat interval used during registration");

static bool discover_count(const std::string& server,
                           const std::string& service_type,
                           int expected_count,
                           const std::string& expected_status,
                           int timeout_sec) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        brpc::Channel channel;
        brpc::ChannelOptions opts;
        opts.timeout_ms = 3000;
        opts.max_retry = 1;
        if (channel.Init(server.c_str(), &opts) != 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            goto check_timeout;
        }

        {
            discovery::DiscoveryService_Stub stub(&channel);
            discovery::DiscoverRequest req;
            discovery::DiscoverResponse rsp;
            brpc::Controller cntl;
            req.set_service_name(service_type);
            stub.Discover(&cntl, &req, &rsp, nullptr);

            if (!cntl.Failed()) {
                int n = rsp.instances_size();
                if (n == expected_count) {
                    bool status_match = true;
                    if (!expected_status.empty()) {
                        for (int i = 0; i < n; ++i) {
                            bool up = (rsp.instances(i).status() == discovery::InstanceStatus::UP);
                            bool down = (rsp.instances(i).status() == discovery::InstanceStatus::DOWN);
                            if ((expected_status == "UP" && !up) ||
                                (expected_status == "DOWN" && !down)) {
                                status_match = false;
                                break;
                            }
                        }
                    }
                    if (status_match) return true;
                }
            }
        }

    check_timeout:
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() >= timeout_sec) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char* argv[]) {
    {
        common::logger::LoggerConfig cfg;
        cfg.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%f:%L] %v";
        common::logger::Initialize(cfg);
    }
    google::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_service_type.empty()) {
        LOG_ERROR << "Usage: test_heartbeat_cycle --server=<addr> --service_type=<type>"
                  << " --host=<ip> --port=<n>";
        return 1;
    }

    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.timeout_ms = 5000;
    opts.max_retry = 1;
    if (channel.Init(FLAGS_server.c_str(), &opts) != 0) {
        LOG_ERROR << "[FAIL] Failed to connect to " << FLAGS_server;
        return 1;
    }

    discovery::DiscoveryService_Stub stub(&channel);

    // === Step 1: Register ===
    std::string instance_id;
    {
        discovery::RegisterRequest req;
        discovery::RegisterResponse rsp;
        brpc::Controller cntl;

        req.mutable_instance()->set_service_name(FLAGS_service_type);
        req.mutable_instance()->set_host(FLAGS_host);
        req.mutable_instance()->set_port(FLAGS_port);
        req.set_heartbeat_interval_sec(FLAGS_heartbeat_interval);

        stub.Register(&cntl, &req, &rsp, nullptr);

        if (cntl.Failed() || !rsp.success() || rsp.instance_id().empty()) {
            LOG_ERROR << "[FAIL] Step 1 (Register): " << cntl.ErrorText();
            return 1;
        }
        instance_id = rsp.instance_id();
        LOG_INFO << "[PASS] Step 1: Registered as " << instance_id;
    }

    // === Step 2: Send heartbeat ===
    {
        discovery::HeartbeatRequest req;
        discovery::HeartbeatResponse rsp;
        brpc::Controller cntl;

        req.set_service_name(FLAGS_service_type);
        req.set_instance_id(instance_id);

        stub.Heartbeat(&cntl, &req, &rsp, nullptr);

        if (cntl.Failed()) {
            LOG_ERROR << "[FAIL] Step 2 (Heartbeat): " << cntl.ErrorText();
            return 1;
        }
        if (!rsp.success()) {
            LOG_ERROR << "[FAIL] Step 2 (Heartbeat): server returned success=false";
            return 1;
        }
        LOG_INFO << "[PASS] Step 2: Heartbeat accepted";
    }

    // === Step 3: Discover, expect UP ===
    {
        if (!discover_count(FLAGS_server, FLAGS_service_type, 1, "UP", 5)) {
            LOG_ERROR << "[FAIL] Step 3: Expected 1 UP instance of ["
                      << FLAGS_service_type << "]";
            return 1;
        }
        LOG_INFO << "[PASS] Step 3: Instance is UP";
    }

    // === Step 4: Stop heartbeating, wait for DOWN ===
    {
        LOG_INFO << "[INFO] Waiting for server to mark instance DOWN (~6s)...";
        if (!discover_count(FLAGS_server, FLAGS_service_type, 1, "DOWN", 15)) {
            LOG_ERROR << "[FAIL] Step 4: Expected 1 DOWN instance of ["
                      << FLAGS_service_type << "]";
            return 1;
        }
        LOG_INFO << "[PASS] Step 4: Instance is DOWN";
    }

    // === Step 5: Wait for cleanup ===
    {
        LOG_INFO << "[INFO] Waiting for server to remove instance (~9s)...";
        if (!discover_count(FLAGS_server, FLAGS_service_type, 0, "", 15)) {
            LOG_ERROR << "[FAIL] Step 5: Expected 0 instances of ["
                      << FLAGS_service_type << "] after cleanup";
            return 1;
        }
        LOG_INFO << "[PASS] Step 5: Instance cleaned up";
    }

    LOG_INFO << "[PASS] test_heartbeat_cycle passed";
    return 0;
}
