#include <brpc/server.h>
#include <json2pb/pb_to_json.h>

#include "feature_service.h"
#include "constant.h"
#include "utils.h"

namespace LinQuickRec {

    FeatureServiceImpl::FeatureServiceImpl(UserFeatureRedis *uf, UserLogRedis *ul)
        :uf_(uf), ul_(ul)
    {}

    void FeatureServiceImpl::GetKRFeatures(google::protobuf::RpcController *cntl_base,
                                           const KRFeatureRequest *req,
                                           KRFeatureResponse *resp,
                                           google::protobuf::Closure *done)

    {
        brpc::ClosureGuard done_guard(done);
        auto *cntl = static_cast<brpc::Controller *>(cntl_base);

        int64_t uid = req->user_id();
        int64_t start = req->log_start();
        int64_t end = req->log_end();

        std::vector<json> logs;
        try {
            logs = ul_->get_slice(uid, start, end);
        }
        catch (const std::exception &ex) {
            cntl->SetFailed(-1, "Redis read failed: %s",
                            ex.what());
            return;
        }
        for (auto &item: logs) {
            auto *lf = resp->add_user_log_features();
            lf->set_video_id(item.value("video_id", 0));
            lf->set_time_ms(item.value("time_ms", 0));
            lf->set_play_time_ms(item.value("play_time_ms", 0));
            lf->set_duration_ms(item.value("duration_ms", 0));

            int64_t action_weight = 0;
            for (auto &[key, weight]: kActionScore) {
                if (item.value(key,0))
                    action_weight +=weight;
            }
            lf->set_action_weights(action_weight);
        }
        if (req->need_user_feature()) {
            auto user_feature = uf_->get(uid);
            auto *uf = resp->mutable_user_features();
            uf->set_user_active_degree(onehot_encode(kUserActiveDegree,
                                  user_feature["user_active_degree"])
            );
            uf->set_follow_user_num_range(
                    onehot_encode(kFollowRange,
                                  user_feature["follow_user_num_range"])
            );
            uf->set_fans_user_num_range(
                    onehot_encode(kFansRange,
                                  user_feature["fans_user_num_range"])
            );
            uf->set_friend_user_num_range(
                    onehot_encode(kFriendRange,
                                  user_feature["friend_user_num_range"])
            );
            uf->set_register_days_range(
                    onehot_encode(kRegisterRange,
                                  user_feature["register_days_range"])
            );
        }
    }
}