//
// Created by lixu on 2026/1/3.
//

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <json2pb/pb_to_json.h>
#include "kr_user_feature_redis.h"
#include "kr_user_log_redis.h"
#include "feature_service.pb.h"

DEFINE_int32(port, 8000, "TCP Port of the feature service server");
DEFINE_string(listen_addr, "", "Server listen address, may be IPV4/IPV6/UDS."
" If this is set, the flag port will be ignored");
DEFINE_bool(enable_checksum, false, "Enable checksum or not");

namespace LinQuickRec {

    const std::unordered_map<std::string, int64_t> kActionScore = {
            {"is_click", 1},
            {"is_like", 2},
            {"is_follow", 4},
            {"is_comment", 8},
            {"is_forward", 16},
            {"is_hate", 32},
            {"long_view", 64},
            {"is_profile_enter", 128},
    };

    const std::vector<std::string> kUserActiveDegree = {
            "UNKNOWN", "day_new", "2_14_day_new", "single_low_active",
            "low_active", "middle_active", "high_active", "full_active", "30day_retention"};
    const std::vector<std::string> kFollowRange = {
            "0", "(0,10]", "(10,50]", "(50,100]", "(100,150]", "(150,250]", "(250,500]", "500+"};
    const std::vector<std::string> kFansRange = {
            "0", "[1,10)", "[10,100)", "[100,1k)", "[1k,5k)", "[5k,1w)", "[1w,10w)", "[10w,100w)", "[100w,1000w)"};
    const std::vector<std::string> kFriendRange = {
            "0", "[1,5)", "[5,30)", "[30,60)", "[60,120)", "[120,250)", "250+"};
    const std::vector<std::string> kRegisterRange = {
            "8-14", "15-30", "31-60", "61-90", "91-180", "181-365", "366-730", "730+"};


    template<typename T>
    int onehot_encode(const T& categories, const std::string& value, int default_idx = 0) {
        auto it = std::find(categories.begin(), categories.end(), value);
        return (it == categories.end()) ? default_idx : static_cast<int>(it - categories.begin());
    }

    class FeatureServiceImpl : public FeatureService {
    public:
        FeatureServiceImpl(UserFeatureRedis* uf, UserLogRedis* ul)
                : uf_(uf), ul_(ul) {}
        ~FeatureServiceImpl() override = default;
        void GetKRFeatures(google::protobuf::RpcController* cntl_base,
                                 const KRFeatureRequest* req,
                                 KRFeatureResponse* resp,
                                 google::protobuf::Closure* done) override {
            // This object helps you to call done->Run() in RAII style. If you need
            // to process the request asynchronously, pass done_guard.release().
            brpc::ClosureGuard done_guard(done);
            auto* cntl = static_cast<brpc::Controller*>(cntl_base);

            int64_t  uid = req->user_id();
            int64_t  start = req->log_start();
            int64_t  end = req->log_end();

            std::vector<json> logs;
            try {
                logs = ul_->get_slice(uid, start, end);
            } catch (const std::exception& ex) {
                cntl->SetFailed(-1, "Redis read failed: %s", ex.what());
                return;
            }
            for (auto& item : logs) {
                auto* lf = resp->add_user_log_features();
                lf->set_video_id(item.value("video_id", 0));
                lf->set_time_ms(item.value("time_ms", 0));
                lf->set_play_time_ms(item.value("play_time_ms", 0));
                lf->set_duration_ms(item.value("duration_ms", 0));
                // 计算 action_weights
                int64_t action_weight = 0;
                for (auto& [key, weight] : kActionScore) {
                    if (item.value(key, 0)) action_weight += weight;
                }
                lf->set_action_weights(action_weight);
            }
            if (req->need_user_feature()) {
                auto user_feature = uf_->get(uid);
                auto* uf = resp->mutable_user_features();
                uf->set_user_active_degree(
                        onehot_encode(kUserActiveDegree,
                                      user_feature["user_active_degree"]));
                uf->set_follow_user_num_range(
                        onehot_encode(kFollowRange,
                                      user_feature["follow_user_num_range"]));
                uf->set_fans_user_num_range(
                        onehot_encode(kFansRange,
                                      user_feature["fans_user_num_range"]));
                uf->set_friend_user_num_range(
                        onehot_encode(kFriendRange,
                                      user_feature["friend_user_num_range"]));
                uf->set_register_days_range(
                        onehot_encode(kRegisterRange,
                                      user_feature["register_days_range"]));
            }

            // Use checksum, only support CRC32C now.
            if (FLAGS_enable_checksum) {
                cntl->set_response_checksum_type(brpc::CHECKSUM_TYPE_CRC32C);
            }
        }
    private:
        UserFeatureRedis* uf_;
        UserLogRedis* ul_;
    };
}  // namespace LinQuickRec

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    brpc::Server server;
    UserFeatureRedis uf;
    UserLogRedis ul;
    LinQuickRec::FeatureServiceImpl feature_service_impl(&uf, &ul);
    if (server.AddService(&feature_service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    butil::EndPoint point;
    if (!FLAGS_listen_addr.empty()) {
        if (butil::str2endpoint(FLAGS_listen_addr.c_str(), &point) < 0) {
            LOG(ERROR) << "Invalid listen address:" << FLAGS_listen_addr;
            return -1;
        }
    } else {
        point = butil::EndPoint(butil::IP_ANY, FLAGS_port);
    }

    // Start the server.
    brpc::ServerOptions options;
    if (server.Start(point, &options) != 0) {
        LOG(ERROR) << "Fail to start FeatureServiceServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
