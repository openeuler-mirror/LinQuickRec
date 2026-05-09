#include "proxy_server.h"
#include "service_discovery.h"

#include "feature.pb.h"
#include "recall.pb.h"
#include "precalc.pb.h"
#include "rank_master.pb.h"
#include "discovery.pb.h"

#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>

#include "common/logger.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

constexpr int DISCOVERY_PORT   = 18100;
constexpr int MOCK_FEATURE_PORT = 18001;
constexpr int MOCK_RECALL_PORT  = 18002;
constexpr int MOCK_PRECALC_PORT = 18003;
constexpr int MOCK_RANK_PORT    = 18004;
constexpr int PROXY_PORT        = 18000;

// ============================================================================
// Mock Discovery Service
// ============================================================================

class MockDiscoveryService : public discovery::DiscoveryService {
public:
    void Register(google::protobuf::RpcController* cntl,
                  const discovery::RegisterRequest* request,
                  discovery::RegisterResponse* response,
                  google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        const auto& inst = request->instance();
        std::lock_guard<std::mutex> lock(mutex_);
        instances_[inst.service_name()].push_back(inst);
        response->set_success(true);
        response->set_instance_id(inst.service_name() + "_" + inst.host() + "_" + std::to_string(inst.port()) + "_1");
        LOG_INFO_STREAM << "Discovery Register: " << inst.service_name()
                        << " at " << inst.host() << ":" << inst.port();
    }

    void Deregister(google::protobuf::RpcController* cntl,
                    const discovery::DeregisterRequest* request,
                    discovery::DeregisterResponse* response,
                    google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_success(true);
    }

    void Heartbeat(google::protobuf::RpcController* cntl,
                   const discovery::HeartbeatRequest* request,
                   discovery::HeartbeatResponse* response,
                   google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_success(true);
        response->set_needs_reregister(false);
    }

    void Discover(google::protobuf::RpcController* cntl,
                  const discovery::DiscoverRequest* request,
                  discovery::DiscoverResponse* response,
                  google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instances_.find(request->service_name());
        if (it != instances_.end()) {
            for (const auto& inst : it->second) {
                auto* added = response->add_instances();
                added->CopyFrom(inst);
                added->set_status(discovery::UP);
            }
        }
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::vector<discovery::ServiceInstance>> instances_;
};

// ============================================================================
// Passing Mock Implementations
// ============================================================================

class MockFeatureService : public feature::FeatureService {
public:
    void GetUserFeatures(google::protobuf::RpcController* cntl,
                         const feature::UserFeatureRequest* request,
                         feature::UserFeatureResponse* response,
                         google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_feature_type(feature::KuaiRand);
        auto* kr_rsp = response->mutable_kr_feat_rsp();
        kr_rsp->set_user_id(request->kr_feat_req().user_id());
        kr_rsp->set_other("mock_feat_other");
        auto* log = kr_rsp->add_user_logs();
        log->add_vec(10);
        log->add_vec(20);
        log->add_vec(30);
    }

    void GetSKUFeatures(google::protobuf::RpcController* cntl,
                        const feature::SKUFeatureRequest* request,
                        feature::SKUFeatureResponse* response,
                        google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
    }
};

class MockRecallService : public recall::RecallService {
public:
    void Recall(google::protobuf::RpcController* cntl,
                const recall::RecallRequest* request,
                recall::RecallResponse* response,
                google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->add_sku_ids(1001);
        response->add_sku_ids(1002);
        response->add_sku_ids(1003);
    }
};

class MockPrecalcService : public precalc::PrecalcService {
public:
    void Precalculate(google::protobuf::RpcController* cntl,
                      const precalc::PrecalcRequest* request,
                      precalc::PrecalcResponse* response,
                      google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_user_feat_key("mock_feat_key_abc123");
        response->set_payload("mock_payload_data");
    }
};

class MockRankService : public rank::RankService {
public:
    void Rank(google::protobuf::RpcController* cntl,
              const rank::RankRequest* request,
              rank::RankResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->add_candidates(1003);
        response->add_candidates(1001);
        response->add_candidates(1002);
    }
};

// ============================================================================
// Failing Mock Implementations
// ============================================================================

class MockFeatureServiceFailing : public feature::FeatureService {
public:
    void GetUserFeatures(google::protobuf::RpcController* cntl,
                         const feature::UserFeatureRequest* request,
                         feature::UserFeatureResponse* response,
                         google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        cntl->SetFailed("mock feature failure");
    }

    void GetSKUFeatures(google::protobuf::RpcController* cntl,
                        const feature::SKUFeatureRequest* request,
                        feature::SKUFeatureResponse* response,
                        google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
    }
};

class MockRecallServiceFailing : public recall::RecallService {
public:
    void Recall(google::protobuf::RpcController* cntl,
                const recall::RecallRequest* request,
                recall::RecallResponse* response,
                google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        cntl->SetFailed("mock recall failure");
    }
};

class MockPrecalcServiceFailing : public precalc::PrecalcService {
public:
    void Precalculate(google::protobuf::RpcController* cntl,
                      const precalc::PrecalcRequest* request,
                      precalc::PrecalcResponse* response,
                      google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        cntl->SetFailed("mock precalc failure");
    }
};

class MockRankServiceFailing : public rank::RankService {
public:
    void Rank(google::protobuf::RpcController* cntl,
              const rank::RankRequest* request,
              rank::RankResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        cntl->SetFailed("mock rank failure");
    }
};

// ============================================================================
// Helpers
// ============================================================================

struct ServerSet {
    brpc::Server discovery_svr;
    brpc::Server feature_svr;
    brpc::Server recall_svr;
    brpc::Server precalc_svr;
    brpc::Server rank_svr;
    brpc::Server proxy_svr;
};

static bool start_server(brpc::Server* server, int port,
                         google::protobuf::Service* service) {
    if (server->AddService(service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG_ERROR_STREAM << "Failed to add service on port " << port;
        return false;
    }
    std::string addr = "0.0.0.0:" + std::to_string(port);
    if (server->Start(addr.c_str(), nullptr) != 0) {
        LOG_ERROR_STREAM << "Failed to start server on " << addr;
        return false;
    }
    return true;
}

static void stop_all(ServerSet& svrs) {
    svrs.proxy_svr.Stop(0);     svrs.proxy_svr.Join();
    svrs.discovery_svr.Stop(0); svrs.discovery_svr.Join();
    svrs.feature_svr.Stop(0);   svrs.feature_svr.Join();
    svrs.recall_svr.Stop(0);    svrs.recall_svr.Join();
    svrs.precalc_svr.Stop(0);   svrs.precalc_svr.Join();
    svrs.rank_svr.Stop(0);      svrs.rank_svr.Join();
}

static void register_to_discovery(discovery::DiscoveryService_Stub& stub,
                                   const std::string& service_name,
                                   const std::string& host, int port) {
    discovery::RegisterRequest req;
    req.mutable_instance()->set_service_name(service_name);
    req.mutable_instance()->set_host(host);
    req.mutable_instance()->set_port(port);
    req.mutable_instance()->set_status(discovery::UP);
    req.set_heartbeat_interval_sec(60);

    discovery::RegisterResponse rsp;
    brpc::Controller cntl;
    stub.Register(&cntl, &req, &rsp, nullptr);
    assert(cntl.Failed() == false);
    assert(rsp.success());
}

static void discovery_register_all(discovery::DiscoveryService_Stub& stub) {
    register_to_discovery(stub, FLAGS_feature_service_name, "127.0.0.1", MOCK_FEATURE_PORT);
    register_to_discovery(stub, FLAGS_recall_service_name,  "127.0.0.1", MOCK_RECALL_PORT);
    register_to_discovery(stub, FLAGS_precalc_service_name, "127.0.0.1", MOCK_PRECALC_PORT);
    register_to_discovery(stub, FLAGS_rank_service_name,    "127.0.0.1", MOCK_RANK_PORT);
}

// ============================================================================
// Test runner
// ============================================================================

struct TestScenario {
    const char*              name;
    feature::FeatureService* feature;
    recall::RecallService*   recall;
    precalc::PrecalcService* precalc;
    rank::RankService*       rank;
    int                      expect_candidates;
    int                      expect_error_code;
};

static void run_scenario(const TestScenario& s) {
    std::cout << "\n--- " << s.name << " ---" << std::endl;

    MockFeatureService    pass_feat;
    MockRecallService     pass_recall;
    MockPrecalcService    pass_precalc;
    MockRankService       pass_rank;
    MockDiscoveryService  mock_discovery;

    auto* f = s.feature ? s.feature : static_cast<feature::FeatureService*>(&pass_feat);
    auto* r = s.recall   ? s.recall   : static_cast<recall::RecallService*>(&pass_recall);
    auto* p = s.precalc  ? s.precalc  : static_cast<precalc::PrecalcService*>(&pass_precalc);
    auto* k = s.rank     ? s.rank     : static_cast<rank::RankService*>(&pass_rank);

    ServerSet svrs;
    assert(start_server(&svrs.discovery_svr, DISCOVERY_PORT, &mock_discovery));
    assert(start_server(&svrs.feature_svr, MOCK_FEATURE_PORT, f));
    assert(start_server(&svrs.recall_svr,  MOCK_RECALL_PORT,  r));
    assert(start_server(&svrs.precalc_svr, MOCK_PRECALC_PORT, p));
    assert(start_server(&svrs.rank_svr,    MOCK_RANK_PORT,    k));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Register mock services with discovery
    {
        brpc::Channel ch;
        brpc::ChannelOptions copts;
        copts.timeout_ms = 3000;
        copts.protocol = "http";
        ch.Init(("127.0.0.1:" + std::to_string(DISCOVERY_PORT)).c_str(), &copts);
        discovery::DiscoveryService_Stub stub(&ch);
        discovery_register_all(stub);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Start proxy
    proxy::ProxyServiceImpl proxy_impl;
    assert(start_server(&svrs.proxy_svr, PROXY_PORT, &proxy_impl));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Send request
    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.timeout_ms      = 10000;
    opts.protocol        = "http";
    opts.connection_type = "pooled";

    std::string proxy_addr = "127.0.0.1:" + std::to_string(PROXY_PORT);
    assert(channel.Init(proxy_addr.c_str(), &opts) == 0);

    proxy::Proxy_Stub stub(&channel);
    proxy::RecommendRequest req;
    req.set_user_id(42);
    req.set_payload("test_payload");

    proxy::RecommendResponse rsp;
    brpc::Controller cntl;
    stub.Recommend(&cntl, &req, &rsp, nullptr);

    assert(!cntl.Failed());
    assert(rsp.error_code() == s.expect_error_code);
    assert(rsp.candidates_size() == s.expect_candidates);

    if (s.expect_error_code != 0) {
        assert(!rsp.error_message().empty());
        std::cout << "[PASS] error_code=" << std::hex << rsp.error_code()
                  << std::dec << " error_message=" << rsp.error_message() << std::endl;
    } else {
        std::cout << "[PASS] candidates=[" << rsp.candidates(0) << ","
                  << rsp.candidates(1) << "," << rsp.candidates(2) << "]"
                  << std::endl;
    }

    stop_all(svrs);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    common::logger::AddConsoleSink();
    std::cout << "=== Proxy Integration Test ===" << std::endl;

    FLAGS_discovery_addr             = "127.0.0.1:" + std::to_string(DISCOVERY_PORT);
    FLAGS_feature_service_name       = "feature_service";
    FLAGS_recall_service_name        = "recall_service";
    FLAGS_precalc_service_name       = "precalc_service";
    FLAGS_rank_service_name          = "rank_service";
    FLAGS_discovery_refresh_interval_ms = 100;
    FLAGS_downstream_max_retries     = 0;
    FLAGS_enable_timing_stats        = false;
    FLAGS_global_thread_pool_size    = 4;
    FLAGS_server_port                = PROXY_PORT;

    // Happy path
    run_scenario({
        "Happy path: all services succeed",
        nullptr, nullptr, nullptr, nullptr,
        3,   // expect 3 candidates
        0    // expect no error
    });

    // Feature fails
    MockFeatureServiceFailing fail_feat;
    run_scenario({
        "Feature service fails",
        &fail_feat, nullptr, nullptr, nullptr,
        0,
        0x01030001
    });

    // Recall fails
    MockRecallServiceFailing fail_recall;
    run_scenario({
        "Recall service fails",
        nullptr, &fail_recall, nullptr, nullptr,
        0,
        0x01030002
    });

    // Precalc fails
    MockPrecalcServiceFailing fail_precalc;
    run_scenario({
        "Precalc service fails",
        nullptr, nullptr, &fail_precalc, nullptr,
        0,
        0x01030003
    });

    // Rank fails
    MockRankServiceFailing fail_rank;
    run_scenario({
        "Rank service fails",
        nullptr, nullptr, nullptr, &fail_rank,
        0,
        0x01030004
    });

    std::cout << "\n=== All Proxy Integration Tests Passed ===" << std::endl;
    return 0;
}
