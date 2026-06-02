#include "feature_server.h"

#include <chrono>
#include <random>
#include <sstream>
#include <thread>

#include "common/logger.h"

DEFINE_int32(server_port, 8001, "Feature service port");
DEFINE_int32(user_log_count, 10, "Number of user logs per response");
DEFINE_int32(user_log_vec_size, 30, "Vector size per user log");
DEFINE_int32(sku_feat_length, 20, "SKU feature string length");
DEFINE_int32(feature_sleep_time_ms, 30, "Feature service simulated sleep time (ms)");

namespace feature {

FeatureServiceImpl::FeatureServiceImpl() {
    LOG_INFO << "FeatureServiceImpl (mock) initialized";
    LOG_INFO << "Feature sleep time: " << FLAGS_feature_sleep_time_ms << " ms";
}

FeatureServiceImpl::~FeatureServiceImpl() = default;

void FeatureServiceImpl::GetUserFeatures(
    google::protobuf::RpcController* /*controller*/,
    const UserFeatureRequest* request,
    UserFeatureResponse* response,
    google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);

    uint64_t user_id = 0;
    if (request->has_kr_feat_req()) {
        user_id = request->kr_feat_req().user_id();
    }

    LOG_INFO << "GetUserFeatures (mock): user_id=" << user_id;

    auto status = process_user_features_request(request, response);
    if (status.IsOk()) {
        LOG_INFO << "GetUserFeatures (mock) response: user_logs="
                  << response->kr_feat_rsp().user_logs_size()
                  << " other=" << response->kr_feat_rsp().other();
    } else {
        response->set_error_code(static_cast<int32_t>(status.Code()));
        response->set_error_message(status.ToString());
        LOG_ERROR << "GetUserFeatures failed: " << status.ToString();
    }
}

void FeatureServiceImpl::GetSKUFeatures(
    google::protobuf::RpcController* /*controller*/,
    const SKUFeatureRequest* request,
    SKUFeatureResponse* response,
    google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);

    LOG_INFO << "GetSKUFeatures (mock): sku_count="
              << request->sku_ids_size();

    auto status = process_sku_features_request(request, response);
    if (status.IsOk()) {
        LOG_INFO << "GetSKUFeatures (mock) response: sku_feats="
                  << response->kr_sku_feats_size();
    } else {
        response->set_error_code(static_cast<int32_t>(status.Code()));
        response->set_error_message(status.ToString());
        LOG_ERROR << "GetSKUFeatures failed: " << status.ToString();
    }
}

common::error::Status FeatureServiceImpl::process_user_features_request(
    const UserFeatureRequest* request,
    UserFeatureResponse* response) {

    uint64_t user_id = 0;
    if (request->has_kr_feat_req()) {
        user_id = request->kr_feat_req().user_id();
    }

    if (user_id == 0) {
        return common::error::Status::Error(
            common::error::feature_errors::EMPTY_USER_ID,
            "Empty user_id in request");
    }

    thread_local std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<uint32_t> val_dist(0, 10000);

    int log_count = FLAGS_user_log_count;
    auto* kr_rsp = response->mutable_kr_feat_rsp();

    for (int i = 0; i < log_count; ++i) {
        auto* log = kr_rsp->add_user_logs();
        for (int j = 0; j < FLAGS_user_log_vec_size; ++j) {
            log->add_vec(val_dist(rng));
        }
    }

    kr_rsp->set_other("mock_feat_" + std::to_string(user_id));
    response->set_feature_type(KuaiRand);

    if (FLAGS_feature_sleep_time_ms > 0) {
        LOG_INFO << "Simulating feature sleep: " << FLAGS_feature_sleep_time_ms << " ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_feature_sleep_time_ms));
    }

    return common::error::Status::OK();
}

common::error::Status FeatureServiceImpl::process_sku_features_request(
    const SKUFeatureRequest* request,
    SKUFeatureResponse* response) {

    if (request->sku_ids_size() == 0) {
        return common::error::Status::Error(
            common::error::feature_errors::EMPTY_SKU_IDS,
            "Empty sku_ids in request");
    }

    thread_local std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<char> char_dist('a', 'z');

    response->set_feature_type(KuaiRand);

    for (int i = 0; i < request->sku_ids_size(); ++i) {
        auto* sku_feat = response->add_kr_sku_feats();
        sku_feat->set_sku_id(request->sku_ids(i));

        std::string feat;
        feat.reserve(FLAGS_sku_feat_length);
        for (int j = 0; j < FLAGS_sku_feat_length; ++j) {
            feat += static_cast<char>(char_dist(rng));
        }
        sku_feat->set_feat(feat);
    }

    if (FLAGS_feature_sleep_time_ms > 0) {
        LOG_INFO << "Simulating feature sleep: " << FLAGS_feature_sleep_time_ms << " ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_feature_sleep_time_ms));
    }

    return common::error::Status::OK();
}

} // namespace feature
