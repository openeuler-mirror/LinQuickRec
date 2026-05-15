#include "proxy_server.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <brpc/server.h>

#include "common/service_discovery.h"

#include "common/logger.h"

DEFINE_int32(server_port, 8080, "Proxy HTTP service port");
DEFINE_int32(feature_timeout_ms, 3000, "Feature service timeout (ms)");
DEFINE_int32(recall_timeout_ms, 5000, "Recall service timeout (ms)");
DEFINE_int32(precalc_timeout_ms, 5000, "Precalc service timeout (ms)");
DEFINE_int32(rank_timeout_ms, 10000, "Rank service timeout (ms)");

DEFINE_string(downstream_connection_type, "pooled",
              "Downstream channel connection type (single/pooled/short)");
DEFINE_int32(downstream_max_retry, 3,
             "Downstream channel BRPC max retry");
DEFINE_int32(downstream_connect_timeout_ms, -1,
             "Downstream channel connect timeout (ms), -1 = disabled");
DEFINE_string(downstream_lb_policy, "",
              "Downstream channel load balancer (rr/wrr/random/la), empty = brpc default");

DEFINE_int32(feature_backup_request_ms, -1,
             "Feature channel backup request (ms), -1 = disabled");
DEFINE_int32(recall_backup_request_ms, -1,
             "Recall channel backup request (ms), -1 = disabled");
DEFINE_int32(precalc_backup_request_ms, -1,
             "Precalc channel backup request (ms), -1 = disabled");
DEFINE_int32(rank_backup_request_ms, -1,
             "Rank channel backup request (ms), -1 = disabled");

DEFINE_int32(server_num_threads, 0,
             "Server bthread num_threads, 0 = BRPC default");
DEFINE_int32(server_idle_timeout_sec, -1,
             "Server idle connection timeout (sec), -1 = BRPC default");
DEFINE_int32(server_max_concurrency, 0,
             "Server max concurrency, 0 = no limit");

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
    LOG_INFO << "ProxyServiceImpl initializing...";

    std::string backend_addr = (FLAGS_registry_backend == "etcd")
        ? FLAGS_etcd_endpoints : FLAGS_discovery_addr;
    service_discovery_ = std::make_unique<common::ServiceDiscovery>(
        FLAGS_registry_backend, backend_addr,
        FLAGS_discovery_refresh_interval_ms);

    LOG_INFO << "ProxyServiceImpl initialized";
    LOG_INFO << "  Discovery: backend=" << FLAGS_registry_backend
             << " address=" << backend_addr;
    LOG_INFO << "  Downstream: connection_type=" << FLAGS_downstream_connection_type
             << " max_retry=" << FLAGS_downstream_max_retry
             << " connect_timeout_ms=" << FLAGS_downstream_connect_timeout_ms;
}

ProxyServiceImpl::~ProxyServiceImpl() {
    LOG_INFO << "ProxyServiceImpl destroyed";
}

namespace {

std::unique_ptr<brpc::Channel> make_channel(
    const std::string& addr, int timeout_ms, int backup_request_ms) {
    auto ch = std::make_unique<brpc::Channel>();
    brpc::ChannelOptions opts;
    opts.timeout_ms = timeout_ms;
    opts.connection_type = FLAGS_downstream_connection_type.c_str();
    opts.max_retry = FLAGS_downstream_max_retry;
    if (FLAGS_downstream_connect_timeout_ms >= 0) {
        opts.connect_timeout_ms = FLAGS_downstream_connect_timeout_ms;
    }
    if (backup_request_ms >= 0) {
        opts.backup_request_ms = backup_request_ms;
    }
    if (ch->Init(addr.c_str(), &opts) != 0) {
        LOG_ERROR << "Failed to init channel to " << addr;
        return nullptr;
    }
    return ch;
}

} // anonymous namespace

void ProxyServiceImpl::Recommend(google::protobuf::RpcController* controller,
                                  const RecommendRequest* request,
                                  RecommendResponse* response,
                                  google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    (void)controller;
    tls_trace_id = generate_trace_id();

    auto status = process_recommend_request(request, response);

    if (!status.IsOk()) {
        response->set_error_code(static_cast<int32_t>(status.Code()));
        response->set_error_message(status.ToString());
        LOG_ERROR << "Request failed: trace_id=" << tls_trace_id
                         << " error=" << status.ToString();
    }
}

common::error::Status ProxyServiceImpl::call_feature_service(
    const RecommendRequest* request,
    feature::UserFeatureResponse* response) {

    std::string host;
    int port;
    std::string instance_id;
    if (!service_discovery_->GetInstance(
            FLAGS_feature_service_name, host, port, instance_id)) {
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0001,
            "FeatureService: no available instance");
    }

    std::string addr = host + ":" + std::to_string(port);
    auto channel = make_channel(addr, FLAGS_feature_timeout_ms,
                                FLAGS_feature_backup_request_ms);
    if (!channel) {
        service_discovery_->ReportFailure(instance_id);
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0001,
            "FeatureService: channel init failed for " + addr);
    }

    feature::UserFeatureRequest feat_req;
    feat_req.set_feature_type(feature::KuaiRand);
    feat_req.mutable_kr_feat_req()->set_user_id(request->user_id());
    feat_req.mutable_kr_feat_req()->set_req_data(request->payload());

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_feature_timeout_ms);
    if (!tls_trace_id.empty()) {
        cntl.set_log_id(std::stoull(tls_trace_id.substr(0, 16), nullptr, 16));
    }

    feature::FeatureService_Stub stub(channel.get());
    stub.GetUserFeatures(&cntl, &feat_req, response, nullptr);

    if (cntl.Failed()) {
        service_discovery_->ReportFailure(instance_id);
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0001,
            "FeatureService: " + cntl.ErrorText());
    }

    service_discovery_->ReportSuccess(instance_id);
    LOG_INFO << "FeatureService success: user_id=" << request->user_id()
             << " user_logs=" << response->kr_feat_rsp().user_logs_size()
             << " instance=" << instance_id;
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::call_recall_service(
    uint64_t user_id,
    const feature::UserFeatureResponse& user_feat,
    recall::RecallResponse* response) {

    std::string host;
    int port;
    std::string instance_id;
    if (!service_discovery_->GetInstance(
            FLAGS_recall_service_name, host, port, instance_id)) {
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0002,
            "RecallService: no available instance");
    }

    std::string addr = host + ":" + std::to_string(port);
    auto channel = make_channel(addr, FLAGS_recall_timeout_ms,
                                FLAGS_recall_backup_request_ms);
    if (!channel) {
        service_discovery_->ReportFailure(instance_id);
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0002,
            "RecallService: channel init failed for " + addr);
    }

    recall::RecallRequest recall_req;
    recall_req.set_user_id(user_id);
    recall_req.set_other(user_feat.kr_feat_rsp().other());
    recall_req.set_trace_id(tls_trace_id);

    for (const auto& log : user_feat.kr_feat_rsp().user_logs()) {
        auto* new_log = recall_req.add_user_logs();
        for (uint32_t v : log.vec()) {
            new_log->add_vec(v);
        }
    }

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_recall_timeout_ms);
    if (!tls_trace_id.empty()) {
        cntl.set_log_id(std::stoull(tls_trace_id.substr(0, 16), nullptr, 16));
    }

    recall::RecallService_Stub stub(channel.get());
    stub.Recall(&cntl, &recall_req, response, nullptr);

    if (cntl.Failed()) {
        service_discovery_->ReportFailure(instance_id);
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0002,
            "RecallService: " + cntl.ErrorText());
    }

    service_discovery_->ReportSuccess(instance_id);
    LOG_INFO << "RecallService success: sku_ids=" << response->sku_ids_size()
             << " instance=" << instance_id;
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::call_precalc_service(
    uint64_t /*user_id*/,
    const feature::UserFeatureResponse& user_feat,
    precalc::PrecalcResponse* response) {

    std::string host;
    int port;
    std::string instance_id;
    if (!service_discovery_->GetInstance(
            FLAGS_precalc_service_name, host, port, instance_id)) {
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0003,
            "PrecalcService: no available instance");
    }

    std::string addr = host + ":" + std::to_string(port);
    auto channel = make_channel(addr, FLAGS_precalc_timeout_ms,
                                FLAGS_precalc_backup_request_ms);
    if (!channel) {
        service_discovery_->ReportFailure(instance_id);
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0003,
            "PrecalcService: channel init failed for " + addr);
    }

    precalc::PrecalcRequest precalc_req;
    precalc_req.set_user_feat(user_feat.kr_feat_rsp().other().empty()
                              ? "user_feat_default"
                              : user_feat.kr_feat_rsp().other());
    precalc_req.set_trace_id(tls_trace_id);

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_precalc_timeout_ms);
    if (!tls_trace_id.empty()) {
        cntl.set_log_id(std::stoull(tls_trace_id.substr(0, 16), nullptr, 16));
    }

    precalc::PrecalcService_Stub stub(channel.get());
    stub.Precalculate(&cntl, &precalc_req, response, nullptr);

    if (cntl.Failed()) {
        service_discovery_->ReportFailure(instance_id);
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0003,
            "PrecalcService: " + cntl.ErrorText());
    }

    service_discovery_->ReportSuccess(instance_id);
    LOG_INFO << "PrecalcService success: user_feat_key=" << response->user_feat_key()
             << " instance=" << instance_id;
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::call_rank_service(
    const recall::RecallResponse& recall_rsp,
    const precalc::PrecalcResponse& precalc_rsp,
    RecommendResponse* response) {

    std::string host;
    int port;
    std::string instance_id;
    if (!service_discovery_->GetInstance(
            FLAGS_rank_service_name, host, port, instance_id)) {
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0004,
            "RankService: no available instance");
    }

    std::string addr = host + ":" + std::to_string(port);
    auto channel = make_channel(addr, FLAGS_rank_timeout_ms,
                                FLAGS_rank_backup_request_ms);
    if (!channel) {
        service_discovery_->ReportFailure(instance_id);
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0004,
            "RankService: channel init failed for " + addr);
    }

    rank::RankMasterRequest rank_req;
    rank_req.set_user_feat_key(precalc_rsp.user_feat_key());
    rank_req.set_trace_id(tls_trace_id);

    std::ostringstream skus_oss;
    for (int i = 0; i < recall_rsp.sku_ids_size(); ++i) {
        skus_oss << std::setw(6) << std::setfill('0') << recall_rsp.sku_ids(i);
    }
    rank_req.set_skus(skus_oss.str());
    rank_req.set_payload(precalc_rsp.payload());

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_rank_timeout_ms);
    if (!tls_trace_id.empty()) {
        cntl.set_log_id(std::stoull(tls_trace_id.substr(0, 16), nullptr, 16));
    }

    rank::RankMasterService_Stub stub(channel.get());
    rank::RankMasterResponse rank_rsp;
    stub.Rank(&cntl, &rank_req, &rank_rsp, nullptr);

    if (cntl.Failed()) {
        service_discovery_->ReportFailure(instance_id);
        return common::error::Status::Error(
            common::error::ModuleCode::GATEWAY,
            common::error::ErrorType::SERVICE_ERROR, 0x0004,
            "RankService: " + cntl.ErrorText());
    }

    service_discovery_->ReportSuccess(instance_id);
    for (int i = 0; i < rank_rsp.candidates_size(); ++i) {
        response->add_candidates(rank_rsp.candidates(i));
    }

    LOG_INFO << "RankService success: candidates=" << response->candidates_size()
             << " instance=" << instance_id;
    return common::error::Status::OK();
}

common::error::Status ProxyServiceImpl::process_recommend_request(
    const RecommendRequest* request,
    RecommendResponse* response) {

    LOG_INFO << "Proxy request received: user_id=" << request->user_id()
             << " trace_id=" << tls_trace_id;

    // Stage 1: Feature
    feature::UserFeatureResponse user_feat;
    auto feat_st = call_feature_service(request, &user_feat);
    if (!feat_st.IsOk()) {
        LOG_ERROR << "Stage 1 (Feature) failed: " << feat_st.ToString();
        return feat_st;
    }

    // Stage 2: Recall + Precalc in parallel
    uint64_t user_id = request->user_id();
    std::string current_trace_id = tls_trace_id;

    auto recall_future = std::async(std::launch::async, [this, user_id, &user_feat, current_trace_id]() {
        tls_trace_id = current_trace_id;
        recall::RecallResponse rsp;
        auto st = call_recall_service(user_id, user_feat, &rsp);
        return std::make_pair(st, std::move(rsp));
    });

    auto precalc_future = std::async(std::launch::async, [this, user_id, &user_feat, current_trace_id]() {
        tls_trace_id = current_trace_id;
        precalc::PrecalcResponse rsp;
        auto st = call_precalc_service(user_id, user_feat, &rsp);
        return std::make_pair(st, std::move(rsp));
    });

    auto [recall_st, recall_rsp] = recall_future.get();
    auto [precalc_st, precalc_rsp] = precalc_future.get();
    if (!recall_st.IsOk() || !precalc_st.IsOk()) {
        LOG_ERROR << "Stage 2 failed: recall="
                  << (recall_st.IsOk() ? "ok" : recall_st.ToString())
                  << " precalc="
                  << (precalc_st.IsOk() ? "ok" : precalc_st.ToString());
        if (!recall_st.IsOk()) return recall_st;
        return precalc_st;
    }

    // Stage 3: Rank
    auto rank_st = call_rank_service(recall_rsp, precalc_rsp, response);
    if (!rank_st.IsOk()) {
        LOG_ERROR << "Stage 3 (Rank) failed: " << rank_st.ToString();
        return rank_st;
    }

    LOG_INFO << "Proxy request completed: user_id=" << request->user_id()
             << " candidates=" << response->candidates_size();
    return common::error::Status::OK();
}

} // namespace proxy
