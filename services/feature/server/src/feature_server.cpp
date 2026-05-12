#include "feature_server.h"

#include <chrono>
#include <sstream>

#include "common/logger.h"

DEFINE_int32(server_port, 8001, "Feature service port");

namespace feature {

FeatureServiceImpl::FeatureServiceImpl()
    : rng_(std::random_device{}()) {
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

    std::uniform_int_distribution<int> log_count_dist(5, 20);
    std::uniform_int_distribution<int> vec_size_dist(10, 50);
    std::uniform_int_distribution<uint32_t> val_dist(0, 10000);

    int log_count = log_count_dist(rng_);
    auto* kr_rsp = response->mutable_kr_feat_rsp();

    for (int i = 0; i < log_count; ++i) {
        auto* log = kr_rsp->add_user_logs();
        int vec_size = vec_size_dist(rng_);
        for (int j = 0; j < vec_size; ++j) {
            log->add_vec(val_dist(rng_));
        }
    }

    kr_rsp->set_other("mock_feat_" + std::to_string(user_id));
    response->set_feature_type(KuaiRand);

    LOG_INFO << "GetUserFeatures (mock) response: user_logs="
              << kr_rsp->user_logs_size()
              << " other=" << kr_rsp->other();
}

void FeatureServiceImpl::GetSKUFeatures(
    google::protobuf::RpcController* controller,
    const SKUFeatureRequest* request,
    SKUFeatureResponse* response,
    google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);

    LOG_INFO << "GetSKUFeatures (mock): sku_count="
              << request->sku_ids_size();

    std::uniform_int_distribution<int> feat_len_dist(10, 30);
    std::uniform_int_distribution<char> char_dist('a', 'z');

    response->set_feature_type(KuaiRand);

    for (int i = 0; i < request->sku_ids_size(); ++i) {
        auto* sku_feat = response->add_kr_sku_feats();
        sku_feat->set_sku_id(request->sku_ids(i));

        int len = feat_len_dist(rng_);
        std::string feat;
        feat.reserve(len);
        for (int j = 0; j < len; ++j) {
            feat += static_cast<char>(char_dist(rng_));
        }
        sku_feat->set_feat(feat);
    }

    LOG_INFO << "GetSKUFeatures (mock) response: sku_feats="
              << response->kr_sku_feats_size();
}

} // namespace feature
