#ifndef FEATURE_SERVER_H
#define FEATURE_SERVER_H

#include <random>
#include <string>

#include <brpc/server.h>

#include "common/global_thread_pool.h"
#include "feature.pb.h"

DECLARE_int32(server_port);

namespace feature {

class FeatureServiceImpl : public FeatureService {
public:
    FeatureServiceImpl();
    ~FeatureServiceImpl();

    void GetUserFeatures(google::protobuf::RpcController* controller,
                         const UserFeatureRequest* request,
                         UserFeatureResponse* response,
                         google::protobuf::Closure* done) override;

    void GetSKUFeatures(google::protobuf::RpcController* controller,
                        const SKUFeatureRequest* request,
                        SKUFeatureResponse* response,
                        google::protobuf::Closure* done) override;

private:
    struct UserFeatureResult {
        bool success = false;
        UserFeatureResponse response;
        std::string error_message;
    };

    struct SKUFeatureResult {
        bool success = false;
        SKUFeatureResponse response;
        std::string error_message;
    };

    UserFeatureResult process_user_features_request(const UserFeatureRequest* request);
    SKUFeatureResult process_sku_features_request(const SKUFeatureRequest* request);

    common::ThreadPool& thread_pool_;
};

} // namespace feature

#endif // FEATURE_SERVER_H
