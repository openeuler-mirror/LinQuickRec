#include "gateway_server.h"

#include <chrono>
#include <sstream>
#include <vector>
#include <cstring>
#include <memory>
#include <algorithm>
#include <functional>
#include <future>

#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>

#include "common/global_thread_pool.h"

DEFINE_int32(server_port, 8080, "Proxy HTTP 服务监听端口");
DEFINE_string(feature_service_addr, "127.0.0.1:8003", "FeatureService 地址");
DEFINE_string(recall_service_addr, "127.0.0.1:8001", "RecallService 地址");
DEFINE_string(precalc_service_addr, "127.0.0.1:8004", "PrecalcService 地址");
DEFINE_string(rank_service_addr, "127.0.0.1:8005", "RankServiceMaster 地址");
DEFINE_int32(feature_timeout_ms, 3000, "Feature 调用超时 (ms)");
DEFINE_int32(recall_timeout_ms, 5000, "Recall 调用超时 (ms)");
DEFINE_int32(precalc_timeout_ms, 5000, "Precalc 调用超时 (ms)");
DEFINE_int32(rank_timeout_ms, 10000, "Rank 调用超时 (ms)");
DEFINE_bool(enable_timing_stats, true, "是否打印阶段时延统计");

namespace proxy {

ProxyServiceImpl::ProxyServiceImpl() {
    LOG(INFO) << "ProxyServiceImpl initializing...";

    init_channel(feature_channel_, FLAGS_feature_service_addr, FLAGS_feature_timeout_ms);
    init_channel(recall_channel_, FLAGS_recall_service_addr, FLAGS_recall_timeout_ms);
    init_channel(precalc_channel_, FLAGS_precalc_service_addr, FLAGS_precalc_timeout_ms);
    init_channel(rank_channel_, FLAGS_rank_service_addr, FLAGS_rank_timeout_ms);

    LOG(INFO) << "ProxyServiceImpl initialized";
    LOG(INFO) << "  FeatureService: " << FLAGS_feature_service_addr;
    LOG(INFO) << "  RecallService:  " << FLAGS_recall_service_addr;
    LOG(INFO) << "  PrecalcService: " << FLAGS_precalc_service_addr;
    LOG(INFO) << "  RankService:    " << FLAGS_rank_service_addr;
}

ProxyServiceImpl::~ProxyServiceImpl() {
    LOG(INFO) << "ProxyServiceImpl destroyed";
}

bool ProxyServiceImpl::init_channel(std::unique_ptr<brpc::Channel>& ch,
                                    const std::string& addr,
                                    int timeout_ms) {
    ch = std::make_unique<brpc::Channel>();
    brpc::ChannelOptions opts;
    opts.timeout_ms = timeout_ms;
    opts.connection_type = "pooled";
    opts.max_retry = 2;

    if (ch->Init(addr.c_str(), &opts) != 0) {
        LOG(ERROR) << "Failed to initialize channel to " << addr;
        return false;
    }
    LOG(INFO) << "Channel initialized to " << addr << " (timeout=" << timeout_ms << "ms)";
    return true;
}

void ProxyServiceImpl::Recommend(const RecommendRequest* request,
                                  RecommendResponse* response,
                                  google::protobuf::Closure* done) {
    auto& pool = common::get_global_thread_pool();

    auto future = pool.submit([this, request]() {
        RecommendResponse local_response;
        process_recommend_request(request, &local_response);
        return local_response;
    });

    try {
        RecommendResponse result = future.get();
        response->CopyFrom(result);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Thread pool task failed: " << e.what();
    }

    brpc::ClosureGuard done_guard(done);
}

bool ProxyServiceImpl::call_feature_service(
    const RecommendRequest* request,
    feature::UserFeatureResponse* response) {

    feature::UserFeatureRequest feat_req;
    feat_req.set_feature_type(feature::KuaiRand);
    feat_req.mutable_kr_feat_req()->set_user_id(request->user_id());
    feat_req.mutable_kr_feat_req()->set_req_data(request->payload());

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_feature_timeout_ms);

    feature::FeatureService_Stub stub(feature_channel_.get());
    stub.GetUserFeatures(&cntl, &feat_req, response, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << "FeatureService call failed: " << cntl.ErrorText();
        return false;
    }

    LOG(INFO) << "FeatureService success: user_id=" << request->user_id()
              << " user_logs=" << response->kr_feat_rsp().user_logs_size();
    return true;
}

bool ProxyServiceImpl::call_recall_service(
    uint64_t user_id,
    const feature::UserFeatureResponse& user_feat,
    recall::RecallResponse* response) {

    recall::RecallRequest recall_req;
    recall_req.set_user_id(user_id);
    recall_req.set_other(user_feat.kr_feat_rsp().other());

    for (const auto& log : user_feat.kr_feat_rsp().user_logs()) {
        auto* new_log = recall_req.add_user_logs();
        for (uint32_t v : log.vec()) {
            new_log->add_vec(v);
        }
    }

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_recall_timeout_ms);

    recall::RecallService_Stub stub(recall_channel_.get());
    stub.Recall(&cntl, &recall_req, response, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << "RecallService call failed: " << cntl.ErrorText();
        return false;
    }

    LOG(INFO) << "RecallService success: sku_ids=" << response->sku_ids_size();
    return true;
}

bool ProxyServiceImpl::call_precalc_service(
    uint64_t user_id,
    const feature::UserFeatureResponse& user_feat,
    precalc::PrecalcResponse* response) {

    precalc::PrecalcRequest precalc_req;
    precalc_req.set_user_feat(user_feat.kr_feat_rsp().other().empty()
                              ? "user_feat_default"
                              : user_feat.kr_feat_rsp().other());

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_precalc_timeout_ms);

    precalc::PrecalcService_Stub stub(precalc_channel_.get());
    stub.Precalculate(&cntl, &precalc_req, response, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << "PrecalcService call failed: " << cntl.ErrorText();
        return false;
    }

    LOG(INFO) << "PrecalcService success: user_feat_key=" << response->user_feat_key();
    return true;
}

bool ProxyServiceImpl::call_rank_service(
    const recall::RecallResponse& recall_rsp,
    const precalc::PrecalcResponse& precalc_rsp,
    RecommendResponse* response) {

    rank::RankRequest rank_req;
    rank_req.set_user_feat_key(precalc_rsp.user_feat_key());

    std::string skus_str;
    for (int i = 0; i < recall_rsp.sku_ids_size(); ++i) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%06lu",
                 static_cast<unsigned long>(recall_rsp.sku_ids(i)));
        skus_str += buf;
    }
    rank_req.set_skus(skus_str);
    rank_req.set_payload(precalc_rsp.payload());

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_rank_timeout_ms);

    rank::RankService_Stub stub(rank_channel_.get());
    rank::RankResponse rank_rsp;
    stub.Rank(&cntl, &rank_req, &rank_rsp, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << "RankService call failed: " << cntl.ErrorText();
        return false;
    }

    for (int i = 0; i < rank_rsp.candidates_size(); ++i) {
        response->add_candidates(rank_rsp.candidates(i));
    }

    LOG(INFO) << "RankService success: candidates=" << response->candidates_size();
    return true;
}

void ProxyServiceImpl::process_recommend_request(
    const RecommendRequest* request,
    RecommendResponse* response) {

    int64_t t0 = butil::gettimeofday_us();

    LOG(INFO) << "Proxy request received: user_id=" << request->user_id();

    // ============================================================
    // Stage 1: 获取特征（同步）
    // ============================================================
    int64_t t1 = t0;
    feature::UserFeatureResponse user_feat;
    bool feat_ok = call_feature_service(request, &user_feat);
    t1 = butil::gettimeofday_us();

    if (!feat_ok) {
        LOG(ERROR) << "Stage 1 (Feature) failed, aborting request";
        return;
    }

    // ============================================================
    // Stage 2: 召回 + 预计算（并行）
    // ============================================================
    uint64_t user_id = request->user_id();

    auto recall_future = std::async(std::launch::async, [this, user_id, &user_feat]() {
        recall::RecallResponse rsp;
        bool ok = call_recall_service(user_id, user_feat, &rsp);
        return std::make_pair(ok, rsp);
    });

    auto precalc_future = std::async(std::launch::async, [this, user_id, &user_feat]() {
        precalc::PrecalcResponse rsp;
        bool ok = call_precalc_service(user_id, user_feat, &rsp);
        return std::make_pair(ok, rsp);
    });

    auto [recall_ok, recall_rsp] = recall_future.get();
    auto [precalc_ok, precalc_rsp] = precalc_future.get();
    int64_t t2 = butil::gettimeofday_us();

    if (!recall_ok || !precalc_ok) {
        LOG(ERROR) << "Stage 2 failed: recall=" << (recall_ok ? "ok" : "fail")
                   << " precalc=" << (precalc_ok ? "ok" : "fail");
        return;
    }

    // ============================================================
    // Stage 3: 精排（同步）
    // ============================================================
    bool rank_ok = call_rank_service(recall_rsp, precalc_rsp, response);
    int64_t t3 = butil::gettimeofday_us();

    if (!rank_ok) {
        LOG(ERROR) << "Stage 3 (Rank) failed";
        return;
    }

    // ============================================================
    // 时延统计
    // ============================================================
    if (FLAGS_enable_timing_stats) {
        LOG(INFO) << "[Proxy Timing] "
                   << " feature=" << (t1 - t0) / 1000.0 << "ms"
                   << " recall+precalc=" << (t2 - t1) / 1000.0 << "ms"
                   << " rank=" << (t3 - t2) / 1000.0 << "ms"
                   << " total=" << (t3 - t0) / 1000.0 << "ms";
    }

    LOG(INFO) << "Proxy request completed: user_id=" << request->user_id()
              << " candidates=" << response->candidates_size();
}

} // namespace proxy
