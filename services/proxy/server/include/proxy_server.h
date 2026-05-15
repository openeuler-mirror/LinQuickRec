#ifndef PROXY_SERVER_H
#define PROXY_SERVER_H

#include <memory>
#include <string>

#include <brpc/channel.h>
#include <gflags/gflags.h>

#include "common/error.h"
#include "feature.pb.h"
#include "precalc.pb.h"
#include "proxy.pb.h"
#include "rank_master.pb.h"
#include "recall.pb.h"

DECLARE_int32(server_port);
DECLARE_string(registry_backend);
DECLARE_string(discovery_addr);
<<<<<<< HEAD
DECLARE_string(etcd_endpoints);
DECLARE_string(feature_service_name);
DECLARE_string(recall_service_name);
DECLARE_string(precalc_service_name);
DECLARE_string(rank_service_name);
=======
>>>>>>> 100ba84d254c4af17901d6680df43936e8f6f4e6
DECLARE_int32(discovery_refresh_interval_ms);

DECLARE_int32(feature_timeout_ms);
DECLARE_int32(recall_timeout_ms);
DECLARE_int32(precalc_timeout_ms);
DECLARE_int32(rank_timeout_ms);

DECLARE_string(downstream_connection_type);
DECLARE_int32(downstream_max_retry);
DECLARE_int32(downstream_connect_timeout_ms);
DECLARE_string(downstream_lb_policy);

DECLARE_int32(feature_backup_request_ms);
DECLARE_int32(recall_backup_request_ms);
DECLARE_int32(precalc_backup_request_ms);
DECLARE_int32(rank_backup_request_ms);

DECLARE_string(feature_service_name);
DECLARE_string(recall_service_name);
DECLARE_string(precalc_service_name);
DECLARE_string(rank_service_name);

DECLARE_int32(server_num_threads);
DECLARE_int32(server_idle_timeout_sec);
DECLARE_int32(server_max_concurrency);

namespace proxy {

class ProxyServiceImpl : public Proxy {
public:
    ProxyServiceImpl();
    ~ProxyServiceImpl();

    void Recommend(google::protobuf::RpcController* controller,
                   const RecommendRequest* request,
                   RecommendResponse* response,
                   google::protobuf::Closure* done) override;

private:
    common::error::Status process_recommend_request(
        const RecommendRequest* request,
        RecommendResponse* response);

    common::error::Status call_feature_service(
        const RecommendRequest* request,
        feature::UserFeatureResponse* response);

    common::error::Status call_recall_service(
        uint64_t user_id,
        const feature::UserFeatureResponse& user_feat,
        recall::RecallResponse* response);

    common::error::Status call_precalc_service(
        uint64_t user_id,
        const feature::UserFeatureResponse& user_feat,
        precalc::PrecalcResponse* response);

    common::error::Status call_rank_service(
        const recall::RecallResponse& recall_rsp,
        const precalc::PrecalcResponse& precalc_rsp,
        RecommendResponse* response);

    std::unique_ptr<brpc::Channel> feature_channel_;
    std::unique_ptr<brpc::Channel> recall_channel_;
    std::unique_ptr<brpc::Channel> precalc_channel_;
    std::unique_ptr<brpc::Channel> rank_channel_;
};

const std::string& get_current_trace_id();

} // namespace proxy

#endif // PROXY_SERVER_H
