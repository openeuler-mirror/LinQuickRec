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

    discovery_ = std::make_unique<ServiceDiscovery>(
        FLAGS_discovery_addr, FLAGS_discovery_refresh_interval_ms);

    LOG_INFO_STREAM << "ProxyServiceImpl initialized";
    LOG_INFO_STREAM << "  Discovery server: " << FLAGS_discovery_addr;
}

ProxyServiceImpl::~ProxyServiceImpl() {
    LOG_INFO_STREAM << "ProxyServiceImpl destroyed";
}

void ProxyServiceImpl::Recommend(const RecommendRequest* request,
                                  RecommendResponse* response,
                                  google::protobuf::Closure* done) {
    tls_trace_id = generate_trace_id();

    auto status = process_recommend_request(request, response);

    if (!status.IsOk()) {
        response->set_error_code(static_cast<int32_t>(status.Code()));
        response->set_error_message(status.ToString());
        LOG_ERROR_STREAM << "Request failed: trace_id=" << tls_trace_id
                         << " error=" << status.ToString();
    }

    brpc::ClosureGuard done_guard(done);
}

common::error::Status ProxyServiceImpl::call_with_retry(
    const std::string& service_name,
    int timeout_ms,
    uint32_t error_specific_code,
    const std::function<common::error::Status(
        brpc::Channel&, brpc::Controller&)>& rpc_impl) {

    std::vector<std::string> tried;

    for (int attempt = 0; attempt <= FLAGS_downstream_max_retries; ++attempt) {
        std::string host;
        int port;
        std::string instance_id;

        if (!discovery_->GetInstance(service_name, host, port, instance_id)) {
            if (attempt == 0) {
                return common::error::Status::Error(
                    common::error::ModuleCode::GATEWAY,
                    common::error::ErrorType::SERVICE_ERROR, error_specific_code,
                    service_name + ": no available instances");
            }
            break;
        }

        std::string addr = host + ":" + std::to_string(port);
        tried.push_back(addr);

        brpc::Channel channel;
        brpc::ChannelOptions opts;
        opts.timeout_ms = timeout_ms;
        opts.connection_type = "pooled";
        opts.max_retry = 0;

        if (channel.Init(addr.c_str(), &opts) != 0) {
            discovery_->ReportFailure(instance_id);
            continue;
        }

        brpc::Controller cntl;
        cntl.set_timeout_ms(timeout_ms);
        cntl.set_log_id(std::stoull(tls_trace_id.substr(0, 16), nullptr, 16));

        auto st = rpc_impl(channel, cntl);

        if (cntl.Failed()) {
            discovery_->ReportFailure(instance_id);
            if (attempt < FLAGS_downstream_max_retries) continue;
            tried.clear();
            st = common::error::Status::Error(
                common::error::ModuleCode::GATEWAY,
                common::error::ErrorType::SERVICE_ERROR, error_specific_code,
                cntl.ErrorText());
        }

        if (st.IsOk()) {
            discovery_->ReportSuccess(instance_id);
            return st;
        }

        if (attempt >= FLAGS_downstream_max_retries) break;
    }

    std::ostringstream msg;
    msg << service_name << ": all " << tried.size() << " instance(s) failed, tried: [";
    for (size_t i = 0; i < tried.size(); ++i) {
        if (i > 0) msg << ", ";
        msg << tried[i];
    }
    msg << "] after " << FLAGS_downstream_max_retries << " retries";

    return common::error::Status::Error(
        common::error::ModuleCode::GATEWAY,
        common::error::ErrorType::SERVICE_ERROR, error_specific_code,
        msg.str());
}

common::error::Status ProxyServiceImpl::call_feature_service(
    const RecommendRequest* request,
    feature::UserFeatureResponse* response) {

    feature::UserFeatureRequest feat_req;
    feat_req.set_feature_type(feature::KuaiRand);
    feat_req.mutable_kr_feat_req()->set_user_id(request->user_id());
    feat_req.mutable_kr_feat_req()->set_req_data(request->payload());

    auto rpc_impl = [&](brpc::Channel& ch, brpc::Controller& cntl) {
        feature::FeatureService_Stub stub(&ch);
        stub.GetUserFeatures(&cntl, &feat_req, response, nullptr);
        return common::error::Status::OK();
    };

    auto st = call_with_retry(FLAGS_feature_service_name,
                              FLAGS_feature_timeout_ms, 0x0001, rpc_impl);

    if (!st.IsOk()) return st;

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

    auto rpc_impl = [&](brpc::Channel& ch, brpc::Controller& cntl) {
        recall::RecallService_Stub stub(&ch);
        stub.Recall(&cntl, &recall_req, response, nullptr);
        return common::error::Status::OK();
    };

    auto st = call_with_retry(FLAGS_recall_service_name,
                              FLAGS_recall_timeout_ms, 0x0002, rpc_impl);

    if (!st.IsOk()) return st;

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

    auto rpc_impl = [&](brpc::Channel& ch, brpc::Controller& cntl) {
        precalc::PrecalcService_Stub stub(&ch);
        stub.Precalculate(&cntl, &precalc_req, response, nullptr);
        return common::error::Status::OK();
    };

    auto st = call_with_retry(FLAGS_precalc_service_name,
                              FLAGS_precalc_timeout_ms, 0x0003, rpc_impl);

    if (!st.IsOk()) return st;

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

    auto rpc_impl = [&](brpc::Channel& ch, brpc::Controller& cntl) {
        rank::RankService_Stub stub(&ch);
        rank::RankResponse rank_rsp;
        stub.Rank(&cntl, &rank_req, &rank_rsp, nullptr);

        if (!cntl.Failed()) {
            for (int i = 0; i < rank_rsp.candidates_size(); ++i) {
                response->add_candidates(rank_rsp.candidates(i));
            }
        }
        return common::error::Status::OK();
    };

    auto st = call_with_retry(FLAGS_rank_service_name,
                              FLAGS_rank_timeout_ms, 0x0004, rpc_impl);

    if (!st.IsOk()) return st;

    LOG_INFO_STREAM << "RankService success: candidates=" << response->candidates_size();
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::process_recommend_request(
    const RecommendRequest* request,
    RecommendResponse* response) {

    auto t0 = std::chrono::steady_clock::now();

    LOG_INFO_STREAM << "Proxy request received: user_id=" << request->user_id()
                    << " trace_id=" << tls_trace_id;

    // Stage 1: 获取特征（同步）
    feature::UserFeatureResponse user_feat;
    auto feat_st = call_feature_service(request, &user_feat);
    auto t1 = std::chrono::steady_clock::now();

    if (!feat_st.IsOk()) {
        LOG_ERROR_STREAM << "Stage 1 (Feature) failed: " << feat_st.ToString();
        return feat_st;
    }

    // Stage 2: 召回 + 预计算（并行，复用全局线程池）
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

    // Stage 3: 精排（同步）
    auto rank_st = call_rank_service(recall_rsp, precalc_rsp, response);
    auto t3 = std::chrono::steady_clock::now();

    if (!rank_st.IsOk()) {
        LOG_ERROR_STREAM << "Stage 3 (Rank) failed: " << rank_st.ToString();
        return rank_st;
    }

    // 时延统计
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
