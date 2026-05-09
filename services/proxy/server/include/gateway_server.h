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
#include <gflags/gflags.h>

#include "common/logger.h"
#include "common/error.h"

#include "service_discovery.h"

#include <functional>
#include <string>
#include <memory>
#include <cstdint>

DECLARE_int32(server_port);
DECLARE_string(discovery_addr);
DECLARE_string(feature_service_name);
DECLARE_string(recall_service_name);
DECLARE_string(precalc_service_name);
DECLARE_string(rank_service_name);
DECLARE_int32(discovery_refresh_interval_ms);
DECLARE_int32(downstream_max_retries);
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

    common::error::Status call_with_retry(
        const std::string& service_name,
        int timeout_ms,
        uint32_t error_specific_code,
        const std::function<common::error::Status(
            brpc::Channel&, brpc::Controller&)>& rpc_impl);

    std::unique_ptr<ServiceDiscovery> discovery_;
};

const std::string& get_current_trace_id();

} // namespace proxy

#endif // GATEWAY_SERVER_H
