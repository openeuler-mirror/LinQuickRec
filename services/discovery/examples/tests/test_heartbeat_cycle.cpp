#include "discovery.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

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
                            bool up = (rsp.instances(i).status() == InstanceStatus::UP);
                            bool down = (rsp.instances(i).status() == InstanceStatus::DOWN);
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
    google::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_service_type.empty()) {
        std::cerr << "Usage: test_heartbeat_cycle --server=<addr> --service_type=<type>"
                  << " --host=<ip> --port=<n>" << std::endl;
        return 1;
    }

    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.timeout_ms = 5000;
    opts.max_retry = 1;
    if (channel.Init(FLAGS_server.c_str(), &opts) != 0) {
        std::cerr << "[FAIL] Failed to connect to " << FLAGS_server << std::endl;
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
            std::cerr << "[FAIL] Step 1 (Register): " << cntl.ErrorText() << std::endl;
            return 1;
        }
        instance_id = rsp.instance_id();
        std::cout << "[PASS] Step 1: Registered as " << instance_id << std::endl;
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
            std::cerr << "[FAIL] Step 2 (Heartbeat): " << cntl.ErrorText() << std::endl;
            return 1;
        }
        if (!rsp.success()) {
            std::cerr << "[FAIL] Step 2 (Heartbeat): server returned success=false" << std::endl;
            return 1;
        }
        std::cout << "[PASS] Step 2: Heartbeat accepted" << std::endl;
    }

    // === Step 3: Discover, expect UP ===
    {
        if (!discover_count(FLAGS_server, FLAGS_service_type, 1, "UP", 5)) {
            std::cerr << "[FAIL] Step 3: Expected 1 UP instance of ["
                      << FLAGS_service_type << "]" << std::endl;
            return 1;
        }
        std::cout << "[PASS] Step 3: Instance is UP" << std::endl;
    }

    // === Step 4: Stop heartbeating, wait for DOWN ===
    // Server checks: last_heartbeat < now - interval*grace_factor(2.0)
    // With interval=3, DOWN after ~6s of no heartbeat. Wait up to 12s.
    {
        std::cout << "[INFO] Waiting for server to mark instance DOWN (~6s)..." << std::endl;
        if (!discover_count(FLAGS_server, FLAGS_service_type, 1, "DOWN", 15)) {
            std::cerr << "[FAIL] Step 4: Expected 1 DOWN instance of ["
                      << FLAGS_service_type << "]" << std::endl;
            return 1;
        }
        std::cout << "[PASS] Step 4: Instance is DOWN" << std::endl;
    }

    // === Step 5: Wait for cleanup ===
    // cleanup_factor=5.0, so removal after ~15s of no heartbeat.
    // We've already waited ~6s from Step 4, so wait up to 15s more.
    {
        std::cout << "[INFO] Waiting for server to remove instance (~9s)..." << std::endl;
        if (!discover_count(FLAGS_server, FLAGS_service_type, 0, "", 15)) {
            std::cerr << "[FAIL] Step 5: Expected 0 instances of ["
                      << FLAGS_service_type << "] after cleanup" << std::endl;
            return 1;
        }
        std::cout << "[PASS] Step 5: Instance cleaned up" << std::endl;
    }

    std::cout << "[PASS] test_heartbeat_cycle passed" << std::endl;
    return 0;
}
