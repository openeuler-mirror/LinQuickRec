#include "feature_server.h"

#include <butil/logging.h>
#include <random>
#include <sstream>

DEFINE_int32(server_port, 8003, "Feature service port");

namespace feature {

FeatureServiceImpl::FeatureServiceImpl()
    : thread_pool_(common::get_global_thread_pool()) {
    LOG(INFO) << "FeatureServiceImpl (mock) initialized with global thread pool size: "
              << thread_pool_.size();
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

    LOG(INFO) << "GetUserFeatures (mock): user_id=" << user_id;

    try {
        auto future = thread_pool_.submit([this, request]() {
            return process_user_features_request(request);
        });

        auto result = future.get();

        if (result.success) {
            response->CopyFrom(result.response);
            LOG(INFO) << "GetUserFeatures (mock) response: user_logs="
                      << result.response.kr_feat_rsp().user_logs_size()
                      << " other=" << result.response.kr_feat_rsp().other();
        } else {
            LOG(ERROR) << "GetUserFeatures failed: " << result.error_message;
            static_cast<brpc::Controller*>(controller)->SetFailed(result.error_message);
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "GetUserFeatures exception: " << e.what();
        static_cast<brpc::Controller*>(controller)->SetFailed(
            std::string("Thread pool error: ") + e.what());
    }
}

void FeatureServiceImpl::GetSKUFeatures(
    google::protobuf::RpcController* controller,
    const SKUFeatureRequest* request,
    SKUFeatureResponse* response,
    google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);

    LOG(INFO) << "GetSKUFeatures (mock): sku_count="
              << request->sku_ids_size();

    try {
        auto future = thread_pool_.submit([this, request]() {
            return process_sku_features_request(request);
        });

        auto result = future.get();

        if (result.success) {
            response->CopyFrom(result.response);
            LOG(INFO) << "GetSKUFeatures (mock) response: sku_feats="
                      << result.response.kr_sku_feats_size();
        } else {
            LOG(ERROR) << "GetSKUFeatures failed: " << result.error_message;
            static_cast<brpc::Controller*>(controller)->SetFailed(result.error_message);
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "GetSKUFeatures exception: " << e.what();
        static_cast<brpc::Controller*>(controller)->SetFailed(
            std::string("Thread pool error: ") + e.what());
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

    std::uniform_int_distribution<int> log_count_dist(5, 20);
    std::uniform_int_distribution<int> vec_size_dist(10, 50);
    std::uniform_int_distribution<uint32_t> val_dist(0, 10000);

    int log_count = log_count_dist(rng);
    auto* kr_rsp = result.response.mutable_kr_feat_rsp();

    for (int i = 0; i < log_count; ++i) {
        auto* log = kr_rsp->add_user_logs();
        int vec_size = vec_size_dist(rng);
        for (int j = 0; j < vec_size; ++j) {
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

    std::uniform_int_distribution<int> feat_len_dist(10, 30);
    std::uniform_int_distribution<char> char_dist('a', 'z');

    result.response.set_feature_type(KuaiRand);

    for (int i = 0; i < request->sku_ids_size(); ++i) {
        auto* sku_feat = result.response.add_kr_sku_feats();
        sku_feat->set_sku_id(request->sku_ids(i));

        int len = feat_len_dist(rng);
        std::string feat;
        feat.reserve(len);
        for (int j = 0; j < len; ++j) {
            feat += static_cast<char>(char_dist(rng));
        }
        sku_feat->set_feat(feat);
    }

    result.success = true;
    return result;
}

} // namespace feature
