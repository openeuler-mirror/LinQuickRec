#include "discovery.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <iostream>
#include <string>
#include <cstdlib>

DEFINE_string(server, "127.0.0.1:8100", "Discovery server address");
DEFINE_string(service_type, "", "Service type (snake_case)");
DEFINE_string(host, "127.0.0.1", "Instance host/IP");
DEFINE_int32(port, 10000, "Instance port");
DEFINE_int32(heartbeat_interval, 10, "Heartbeat interval (seconds)");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_service_type.empty()) {
        std::cerr << "Usage: test_register --server=<addr> --service_type=<type>"
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

    // --- Register ---
    {
        discovery::RegisterRequest req;
        discovery::RegisterResponse rsp;
        brpc::Controller cntl;

        req.mutable_instance()->set_service_name(FLAGS_service_type);
        req.mutable_instance()->set_host(FLAGS_host);
        req.mutable_instance()->set_port(FLAGS_port);
        req.set_heartbeat_interval_sec(FLAGS_heartbeat_interval);

        stub.Register(&cntl, &req, &rsp, nullptr);

        if (cntl.Failed()) {
            std::cerr << "[FAIL] Register RPC failed: " << cntl.ErrorText() << std::endl;
            return 1;
        }
        if (!rsp.success()) {
            std::cerr << "[FAIL] Register returned success=false: " << rsp.message() << std::endl;
            return 1;
        }
        if (rsp.instance_id().empty()) {
            std::cerr << "[FAIL] Register returned empty instance_id" << std::endl;
            return 1;
        }

        std::string instance_id = rsp.instance_id();
        std::cout << "[PASS] Registered as " << instance_id << std::endl;

        // --- Deregister ---
        discovery::DeregisterRequest dereg_req;
        discovery::DeregisterResponse dereg_rsp;
        brpc::Controller dereg_cntl;

        dereg_req.set_service_name(FLAGS_service_type);
        dereg_req.set_instance_id(instance_id);

        stub.Deregister(&dereg_cntl, &dereg_req, &dereg_rsp, nullptr);

        if (dereg_cntl.Failed()) {
            std::cerr << "[FAIL] Deregister RPC failed: " << dereg_cntl.ErrorText() << std::endl;
            return 1;
        }
        if (!dereg_rsp.success()) {
            std::cerr << "[FAIL] Deregister returned success=false: " << dereg_rsp.message() << std::endl;
            return 1;
        }

        std::cout << "[PASS] Deregistered " << instance_id << std::endl;
    }

    // --- Verify instance is gone ---
    {
        discovery::DiscoverRequest req;
        discovery::DiscoverResponse rsp;
        brpc::Controller cntl;

        req.set_service_name(FLAGS_service_type);
        stub.Discover(&cntl, &req, &rsp, nullptr);

        if (cntl.Failed()) {
            std::cerr << "[FAIL] Discover RPC failed: " << cntl.ErrorText() << std::endl;
            return 1;
        }

        if (rsp.instances_size() != 0) {
            std::cerr << "[FAIL] Expected 0 instances after deregister, got "
                      << rsp.instances_size() << std::endl;
            return 1;
        }

        std::cout << "[PASS] Confirmed 0 instances of [" << FLAGS_service_type << "]"
                  << std::endl;
    }

    std::cout << "[PASS] test_register passed" << std::endl;
    return 0;
}
