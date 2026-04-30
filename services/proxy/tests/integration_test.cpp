#include "gateway_server.h"

#include "feature.pb.h"
#include "recall.pb.h"
#include "precalc.pb.h"
#include "rank_master.pb.h"

#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>

#include "common/logger.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

constexpr int MOCK_FEATURE_PORT = 18001;
constexpr int MOCK_RECALL_PORT  = 18002;
constexpr int MOCK_PRECALC_PORT = 18003;
constexpr int MOCK_RANK_PORT    = 18004;
constexpr int PROXY_PORT        = 18000;

// ============================================================================
// Mock Service Implementations
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
// Helper: start a brpc server on a given port
// ============================================================================

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
    LOG_INFO_STREAM << "Server started on " << addr;
    return true;
}

// ============================================================================
// Main: single-process integration test
// ============================================================================

int main(int argc, char* argv[]) {
    common::logger::AddConsoleSink();
    std::cout << "=== Proxy Integration Test ===" << std::endl;

    // -----------------------------------------------------------------------
    // 1. Point proxy gflags to local mock servers
    // -----------------------------------------------------------------------
    FLAGS_feature_service_addr = "127.0.0.1:" + std::to_string(MOCK_FEATURE_PORT);
    FLAGS_recall_service_addr  = "127.0.0.1:" + std::to_string(MOCK_RECALL_PORT);
    FLAGS_precalc_service_addr = "127.0.0.1:" + std::to_string(MOCK_PRECALC_PORT);
    FLAGS_rank_service_addr    = "127.0.0.1:" + std::to_string(MOCK_RANK_PORT);
    FLAGS_server_port          = PROXY_PORT;
    FLAGS_enable_timing_stats  = false;
    // shrink timeouts for test
    FLAGS_global_thread_pool_size = 4;

    // -----------------------------------------------------------------------
    // 2. Start mock downstream servers
    // -----------------------------------------------------------------------
    MockFeatureService mock_feature;
    MockRecallService  mock_recall;
    MockPrecalcService mock_precalc;
    MockRankService    mock_rank;

    brpc::Server feature_svr, recall_svr, precalc_svr, rank_svr;
    assert(start_server(&feature_svr, MOCK_FEATURE_PORT, &mock_feature));
    assert(start_server(&recall_svr,  MOCK_RECALL_PORT,  &mock_recall));
    assert(start_server(&precalc_svr, MOCK_PRECALC_PORT, &mock_precalc));
    assert(start_server(&rank_svr,    MOCK_RANK_PORT,    &mock_rank));
    std::cout << "[PASS] All mock servers started" << std::endl;

    // -----------------------------------------------------------------------
    // 3. Start proxy server
    // -----------------------------------------------------------------------
    proxy::ProxyServiceImpl proxy_impl;
    brpc::Server proxy_svr;
    assert(start_server(&proxy_svr, PROXY_PORT, &proxy_impl));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::cout << "[PASS] Proxy server started on port " << PROXY_PORT << std::endl;

    // -----------------------------------------------------------------------
    // 4. Send a Recommend request and verify response
    // -----------------------------------------------------------------------
    {
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
        assert(rsp.candidates_size() == 3);
        // Rank service re-orders: [1003, 1001, 1002]
        assert(rsp.candidates(0) == 1003);
        assert(rsp.candidates(1) == 1001);
        assert(rsp.candidates(2) == 1002);

        std::cout << "[PASS] Recommend RPC succeeded"
                  << "  candidates=["
                  << rsp.candidates(0) << ","
                  << rsp.candidates(1) << ","
                  << rsp.candidates(2) << "]" << std::endl;
    }

    // -----------------------------------------------------------------------
    // 5. Cleanup
    // -----------------------------------------------------------------------
    proxy_svr.Stop(0);    proxy_svr.Join();
    feature_svr.Stop(0);  feature_svr.Join();
    recall_svr.Stop(0);   recall_svr.Join();
    precalc_svr.Stop(0);  precalc_svr.Join();
    rank_svr.Stop(0);     rank_svr.Join();

    std::cout << "\n=== All Proxy Integration Tests Passed ===" << std::endl;
    return 0;
}
