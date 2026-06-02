#ifndef FEATURE_SERVER_H
#define FEATURE_SERVER_H

#include <random>
#include <string>

#include <brpc/server.h>

#include "common/error.h"
#include "feature.pb.h"

DECLARE_int32(server_port);
DECLARE_int32(feature_sleep_time_ms);

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
    common::error::Status process_user_features_request(
        const UserFeatureRequest* request,
        UserFeatureResponse* response);

    common::error::Status process_sku_features_request(
        const SKUFeatureRequest* request,
        SKUFeatureResponse* response);
};

} // namespace feature

#endif // FEATURE_SERVER_H
