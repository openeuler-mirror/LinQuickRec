#ifndef GATEWAY_SERVER_H
#define GATEWAY_SERVER_H

#include "proxy.pb.h"
#include "feature.pb.h"
#include "recall.pb.h"
#include "precalc.pb.h"
#include "rank_master.pb.h"

#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

DECLARE_int32(server_port);
DECLARE_string(feature_service_addr);
DECLARE_string(recall_service_addr);
DECLARE_string(precalc_service_addr);
DECLARE_string(rank_service_addr);
DECLARE_int32(feature_timeout_ms);
DECLARE_int32(recall_timeout_ms);
DECLARE_int32(precalc_timeout_ms);
DECLARE_int32(rank_timeout_ms);
DECLARE_bool(enable_timing_stats);

namespace proxy {

class ProxyServiceImpl : public Proxy {
public:
    ProxyServiceImpl();
    ~ProxyServiceImpl();

    void Recommend(const RecommendRequest* request,
                   RecommendResponse* response,
                   google::protobuf::Closure* done) override;

private:
    void process_recommend_request(const RecommendRequest* request,
                                   RecommendResponse* response);

    bool call_feature_service(const RecommendRequest* request,
                              feature::UserFeatureResponse* response);

    bool call_recall_service(uint64_t user_id,
                             const feature::UserFeatureResponse& user_feat,
                             recall::RecallResponse* response);

    bool call_precalc_service(uint64_t user_id,
                               const feature::UserFeatureResponse& user_feat,
                               precalc::PrecalcResponse* response);

    bool call_rank_service(const recall::RecallResponse& recall_rsp,
                           const precalc::PrecalcResponse& precalc_rsp,
                           RecommendResponse* response);

    bool init_channel(std::unique_ptr<brpc::Channel>& ch,
                      const std::string& addr,
                      int timeout_ms);

    std::unique_ptr<brpc::Channel> feature_channel_;
    std::unique_ptr<brpc::Channel> recall_channel_;
    std::unique_ptr<brpc::Channel> precalc_channel_;
    std::unique_ptr<brpc::Channel> rank_channel_;
};

} // namespace proxy

#endif // GATEWAY_SERVER_H
