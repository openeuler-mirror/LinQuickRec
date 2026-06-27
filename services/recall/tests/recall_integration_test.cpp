// Recall 服务集成测试
// 单进程启动 mock vLLM + RecallServiceImpl，通过 BRPC RPC 端到端验证

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <gflags/gflags.h>

#include "common/error.h"
#include "common/logger.h"
#include "recall_server.h"

constexpr int MOCK_VLLM_PORT = 18000;
constexpr int RECALL_PORT    = 18002;

// ============================================================================
// Mock vLLM HTTP Server (raw socket)
// ============================================================================

class MockVllmServer {
public:
    explicit MockVllmServer(int port) : port_(port) {}

    void SetResponse(const std::string& body, int status = 200) {
        response_body_ = body;
        http_status_ = status;
    }

    void Start() {
        running_ = true;
        thread_ = std::thread(&MockVllmServer::serve, this);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void Stop() {
        running_ = false;
        if (fd_ >= 0) {
            shutdown(fd_, SHUT_RDWR);
            close(fd_);
            fd_ = -1;
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    static std::string build_http_response(int status, const std::string& body) {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " OK\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << body;
        return oss.str();
    }

    void serve() {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd_ >= 0);
        int opt = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port_);
        int ret = bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        assert(ret == 0);
        ret = listen(fd_, 5);
        assert(ret == 0);

        while (running_) {
            int client = accept(fd_, nullptr, nullptr);
            if (client < 0) break;

            char buf[4096];
            ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
            (void)n;

            std::string resp = build_http_response(http_status_, response_body_);
            send(client, resp.c_str(), resp.size(), 0);
            close(client);
        }
    }

    int port_;
    std::string response_body_;
    int http_status_ = 200;
    std::atomic<bool> running_{false};
    std::thread thread_;
    int fd_ = -1;
};

// ============================================================================
// Helpers
// ============================================================================

struct Scenario {
    const char* name;
    bool start_mock;
    std::string mock_body;
    int expect_error_code;
    int expect_sku_count;
    std::vector<uint64_t> expect_prefix;
};

static void run_scenario(const Scenario& s) {
    std::cout << "\n--- " << s.name << " ---" << std::endl;

    MockVllmServer mock(MOCK_VLLM_PORT);
    if (s.start_mock) {
        mock.SetResponse(s.mock_body);
        mock.Start();
    }

    FLAGS_vllm_base_url = std::string("http://127.0.0.1:") + std::to_string(MOCK_VLLM_PORT);
    FLAGS_enable_vllm = true;
    FLAGS_sku_count = 5;
    FLAGS_vllm_timeout_ms = 3000;
    FLAGS_vllm_max_retry = 0;
    FLAGS_recall_sleep_time_ms = 0;
    FLAGS_recall_payload_size_kb = 0;

    recall::RecallServiceImpl service;
    brpc::Server server;
    assert(server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE) == 0);
    std::string addr = "127.0.0.1:" + std::to_string(RECALL_PORT);
    assert(server.Start(addr.c_str(), nullptr) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.timeout_ms = 5000;
    opts.protocol = "baidu_std";
    assert(channel.Init(addr.c_str(), &opts) == 0);
    recall::RecallService_Stub stub(&channel);

    recall::RecallRequest req;
    req.set_user_id(42);
    recall::RecallResponse rsp;
    brpc::Controller cntl;
    stub.Recall(&cntl, &req, &rsp, nullptr);

    assert(!cntl.Failed());
    assert(rsp.error_code() == s.expect_error_code);
    assert(rsp.sku_ids_size() == s.expect_sku_count);

    for (size_t i = 0; i < s.expect_prefix.size(); ++i) {
        assert(rsp.sku_ids(i) == s.expect_prefix[i]);
    }

    if (s.expect_sku_count > 0 && s.expect_prefix.size() < static_cast<size_t>(s.expect_sku_count)) {
        std::unordered_set<uint64_t> seen;
        for (int i = 0; i < rsp.sku_ids_size(); ++i) {
            assert(seen.insert(rsp.sku_ids(i)).second);
        }
    }

    std::cout << "[PASS]" << std::endl;

    server.Stop(0); server.Join();
    if (s.start_mock) mock.Stop();
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    common::logger::AddConsoleSink();

    std::cout << "=== Recall Service Integration Tests ===" << std::endl;

    using namespace common::error;

    run_scenario({
        "Happy path: vLLM returns valid SKUs",
        true,
        R"({"choices":[{"message":{"content":"123456,234567,345678,456789,567890"}}]})",
        static_cast<int>(OK_CODE),
        5,
        {123456, 234567, 345678, 456789, 567890}
    });

    run_scenario({
        "vLLM unreachable (no mock server)",
        false,
        "",
        static_cast<int>(recall_errors::VLLM_REQUEST_FAILED),
        0,
        {}
    });

    run_scenario({
        "vLLM returns malformed JSON",
        true,
        "{ this is not valid json",
        static_cast<int>(recall_errors::VLLM_RESPONSE_PARSE_FAILED),
        0,
        {}
    });

    run_scenario({
        "vLLM returns empty choices array",
        true,
        R"({"choices":[]})",
        static_cast<int>(recall_errors::VLLM_RESPONSE_PARSE_FAILED),
        0,
        {}
    });

    run_scenario({
        "vLLM returns fewer SKUs + random fill to target",
        true,
        R"({"choices":[{"message":{"content":"111111,222222,333333"}}]})",
        static_cast<int>(OK_CODE),
        5,
        {111111, 222222, 333333}
    });

    std::cout << "\n=== All Integration Tests Passed ===" << std::endl;
    return 0;
}
