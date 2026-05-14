#include "feature_server.h"

#include <random>
#include <sstream>

#include "common/logger.h"

DEFINE_int32(server_port, 8001, "Feature service port");
DEFINE_int32(user_log_count, 10, "Number of user logs per response");
DEFINE_int32(user_log_vec_size, 30, "Vector size per user log");
DEFINE_int32(sku_feat_length, 20, "SKU feature string length");

namespace feature {

FeatureServiceImpl::FeatureServiceImpl() {
    LOG_INFO << "FeatureServiceImpl (mock) initialized";
}

FeatureServiceImpl::~FeatureServiceImpl() = default;

void FeatureServiceImpl::GetUserFeatures(
    google::protobuf::RpcController* controller,
    const UserFeatureRequest* request,
    UserFeatureResponse* response,
    google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);

    uint64_t user_id = 0;
    if (request->has_kr_feat_req()) {
        user_id = request->kr_feat_req().user_id();
    }

    LOG_INFO << "GetUserFeatures (mock): user_id=" << user_id;

    auto result = process_user_features_request(request);
    if (result.success) {
        response->CopyFrom(result.response);
        LOG_INFO << "GetUserFeatures (mock) response: user_logs="
                  << result.response.kr_feat_rsp().user_logs_size()
                  << " other=" << result.response.kr_feat_rsp().other();
    } else {
        LOG_ERROR << "GetUserFeatures failed: " << result.error_message;
        static_cast<brpc::Controller*>(controller)->SetFailed(result.error_message);
    }
}

void FeatureServiceImpl::GetSKUFeatures(
    google::protobuf::RpcController* controller,
    const SKUFeatureRequest* request,
    SKUFeatureResponse* response,
    google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);

    LOG_INFO << "GetSKUFeatures (mock): sku_count="
              << request->sku_ids_size();

    auto result = process_sku_features_request(request);
    if (result.success) {
        response->CopyFrom(result.response);
        LOG_INFO << "GetSKUFeatures (mock) response: sku_feats="
                  << result.response.kr_sku_feats_size();
    } else {
        LOG_ERROR << "GetSKUFeatures failed: " << result.error_message;
        static_cast<brpc::Controller*>(controller)->SetFailed(result.error_message);
    }
}

FeatureServiceImpl::UserFeatureResult
FeatureServiceImpl::process_user_features_request(const UserFeatureRequest* request) {
    UserFeatureResult result;

    uint64_t user_id = 0;
    if (request->has_kr_feat_req()) {
        user_id = request->kr_feat_req().user_id();
    }

    thread_local std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<uint32_t> val_dist(0, 10000);

    int log_count = FLAGS_user_log_count;
    auto* kr_rsp = result.response.mutable_kr_feat_rsp();

    for (int i = 0; i < log_count; ++i) {
        auto* log = kr_rsp->add_user_logs();
        for (int j = 0; j < FLAGS_user_log_vec_size; ++j) {
            log->add_vec(val_dist(rng));
        }
    }

    kr_rsp->set_other("mock_feat_" + std::to_string(user_id));
    result.response.set_feature_type(KuaiRand);

    result.success = true;
    return result;
}

FeatureServiceImpl::SKUFeatureResult
FeatureServiceImpl::process_sku_features_request(const SKUFeatureRequest* request) {
    SKUFeatureResult result;

    thread_local std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<char> char_dist('a', 'z');

    result.response.set_feature_type(KuaiRand);

    for (int i = 0; i < request->sku_ids_size(); ++i) {
        auto* sku_feat = result.response.add_kr_sku_feats();
        sku_feat->set_sku_id(request->sku_ids(i));

        std::string feat;
        feat.reserve(FLAGS_sku_feat_length);
        for (int j = 0; j < FLAGS_sku_feat_length; ++j) {
            feat += static_cast<char>(char_dist(rng));
        }
        sku_feat->set_feat(feat);
    }

    result.success = true;
    return result;
}

} // namespace feature
