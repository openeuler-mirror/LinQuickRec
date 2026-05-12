#ifndef FEATURE_SERVER_H
#define FEATURE_SERVER_H

#include <random>

#include <brpc/server.h>

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
    std::mt19937 rng_;
};

} // namespace feature

#endif // FEATURE_SERVER_H
