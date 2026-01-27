#ifndef LINQUICKREC_FEATURE_SERVICE_H
#define LINQUICKREC_FEATURE_SERVICE_H

#include "kr_user_log_redis.h"
#include "kr_user_feature_redis.h"
#include "feature_service.pb.h"

namespace LinQuickRec {

    class FeatureServiceImpl : public FeatureService {
    public:
        FeatureServiceImpl(UserFeatureRedis *uf, UserLogRedis *ul);
        ~FeatureServiceImpl() override = default;
        void GetKRFeatures(google::protobuf::RpcController *cntl_base,
                           const KRFeatureRequest *req,
                           KRFeatureResponse *resp,
                           google::protobuf::Closure *done) override;
    private:
        UserFeatureRedis *uf_;
        UserLogRedis *ul_;
    };

}
#endif //LINQUICKREC_FEATURE_SERVICE_H
