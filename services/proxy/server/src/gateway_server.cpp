#include "gateway_server.h"

#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>
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

namespace {

thread_local std::string tls_trace_id;

std::string generate_trace_id() {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();

    static thread_local std::mt19937_64 rng(std::random_device{}());
    uint64_t rand_val = rng();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << us
        << std::setw(16) << rand_val;
    return oss.str();
}

} // anonymous namespace

namespace proxy {

const std::string& get_current_trace_id() {
    return tls_trace_id;
}

} // namespace proxy

namespace proxy {

ProxyServiceImpl::ProxyServiceImpl() {
    LOG_INFO_STREAM << "ProxyServiceImpl initializing...";

    init_channel(feature_channel_, FLAGS_feature_service_addr, FLAGS_feature_timeout_ms);
    init_channel(recall_channel_, FLAGS_recall_service_addr, FLAGS_recall_timeout_ms);
    init_channel(precalc_channel_, FLAGS_precalc_service_addr, FLAGS_precalc_timeout_ms);
    init_channel(rank_channel_, FLAGS_rank_service_addr, FLAGS_rank_timeout_ms);

    LOG_INFO_STREAM << "ProxyServiceImpl initialized";
    LOG_INFO_STREAM << "  FeatureService: " << FLAGS_feature_service_addr;
    LOG_INFO_STREAM << "  RecallService:  " << FLAGS_recall_service_addr;
    LOG_INFO_STREAM << "  PrecalcService: " << FLAGS_precalc_service_addr;
    LOG_INFO_STREAM << "  RankService:    " << FLAGS_rank_service_addr;
}

ProxyServiceImpl::~ProxyServiceImpl() {
    LOG_INFO_STREAM << "ProxyServiceImpl destroyed";
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
        LOG_ERROR_STREAM << "Failed to initialize channel to " << addr;
        return false;
    }
    LOG_INFO_STREAM << "Channel initialized to " << addr << " (timeout=" << timeout_ms << "ms)";
    return true;
}

void ProxyServiceImpl::Recommend(const RecommendRequest* request,
                                  RecommendResponse* response,
                                  google::protobuf::Closure* done) {
    tls_trace_id = generate_trace_id();

    auto status = process_recommend_request(request, response);

    if (!status.IsOk()) {
        LOG_ERROR_STREAM << "Request failed: " << status.ToString();
    }

    brpc::ClosureGuard done_guard(done);
}

common::error::Status ProxyServiceImpl::call_feature_service(
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
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0001,
            std::string("FeatureService: ") + cntl.ErrorText());
    }

    LOG_INFO_STREAM << "FeatureService success: user_id=" << request->user_id()
              << " user_logs=" << response->kr_feat_rsp().user_logs_size();
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::call_recall_service(
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
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0002,
            std::string("RecallService: ") + cntl.ErrorText());
    }

    LOG_INFO_STREAM << "RecallService success: sku_ids=" << response->sku_ids_size();
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::call_precalc_service(
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
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0003,
            std::string("PrecalcService: ") + cntl.ErrorText());
    }

    LOG_INFO_STREAM << "PrecalcService success: user_feat_key=" << response->user_feat_key();
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::call_rank_service(
    const recall::RecallResponse& recall_rsp,
    const precalc::PrecalcResponse& precalc_rsp,
    RecommendResponse* response) {

    rank::RankRequest rank_req;
    rank_req.set_user_feat_key(precalc_rsp.user_feat_key());

    std::ostringstream skus_oss;
    for (int i = 0; i < recall_rsp.sku_ids_size(); ++i) {
        skus_oss << std::setw(6) << std::setfill('0') << recall_rsp.sku_ids(i);
    }
    rank_req.set_skus(skus_oss.str());
    rank_req.set_payload(precalc_rsp.payload());

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_rank_timeout_ms);

    rank::RankService_Stub stub(rank_channel_.get());
    rank::RankResponse rank_rsp;
    stub.Rank(&cntl, &rank_req, &rank_rsp, nullptr);

    if (cntl.Failed()) {
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0004,
            std::string("RankService: ") + cntl.ErrorText());
    }

    for (int i = 0; i < rank_rsp.candidates_size(); ++i) {
        response->add_candidates(rank_rsp.candidates(i));
    }

    LOG_INFO_STREAM << "RankService success: candidates=" << response->candidates_size();
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::process_recommend_request(
    const RecommendRequest* request,
    RecommendResponse* response) {

    auto t0 = std::chrono::steady_clock::now();

    LOG_INFO_STREAM << "Proxy request received: user_id=" << request->user_id()
                    << " trace_id=" << tls_trace_id;

    // ============================================================
    // Stage 1: 获取特征（同步）
    // ============================================================
    feature::UserFeatureResponse user_feat;
    auto feat_st = call_feature_service(request, &user_feat);
    auto t1 = std::chrono::steady_clock::now();

    if (!feat_st.IsOk()) {
        LOG_ERROR_STREAM << "Stage 1 (Feature) failed: " << feat_st.ToString();
        return feat_st;
    }

    // ============================================================
    // Stage 2: 召回 + 预计算（并行）
    // ============================================================
    uint64_t user_id = request->user_id();

    auto& pool = common::get_global_thread_pool();

    auto recall_future = pool.submit([this, user_id, &user_feat]() {
        recall::RecallResponse rsp;
        auto st = call_recall_service(user_id, user_feat, &rsp);
        return std::make_pair(st, std::move(rsp));
    });

    auto precalc_future = pool.submit([this, user_id, &user_feat]() {
        precalc::PrecalcResponse rsp;
        auto st = call_precalc_service(user_id, user_feat, &rsp);
        return std::make_pair(st, std::move(rsp));
    });

    auto [recall_st, recall_rsp] = recall_future.get();
    auto [precalc_st, precalc_rsp] = precalc_future.get();
    auto t2 = std::chrono::steady_clock::now();

    if (!recall_st.IsOk() || !precalc_st.IsOk()) {
        LOG_ERROR_STREAM << "Stage 2 failed: recall="
                         << (recall_st.IsOk() ? "ok" : recall_st.ToString())
                         << " precalc="
                         << (precalc_st.IsOk() ? "ok" : precalc_st.ToString());
        if (!recall_st.IsOk()) return recall_st;
        return precalc_st;
    }

    // ============================================================
    // Stage 3: 精排（同步）
    // ============================================================
    auto rank_st = call_rank_service(recall_rsp, precalc_rsp, response);
    auto t3 = std::chrono::steady_clock::now();

    if (!rank_st.IsOk()) {
        LOG_ERROR_STREAM << "Stage 3 (Rank) failed: " << rank_st.ToString();
        return rank_st;
    }

    // ============================================================
    // 时延统计
    // ============================================================
    if (FLAGS_enable_timing_stats) {
        auto feat_us   = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        auto stage2_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        auto rank_us   = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
        auto total_us  = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t0).count();

        LOG_INFO_STREAM << "[Proxy Timing] "
                   << " feature=" << feat_us / 1000.0 << "ms"
                   << " recall+precalc=" << stage2_us / 1000.0 << "ms"
                   << " rank=" << rank_us / 1000.0 << "ms"
                   << " total=" << total_us / 1000.0 << "ms";
    }

    LOG_INFO_STREAM << "Proxy request completed: user_id=" << request->user_id()
              << " candidates=" << response->candidates_size();
    return common::error::Status::OK();
}

} // namespace proxy
