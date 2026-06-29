#include "rank_master_server.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <datasystem/kv_client.h>

#include "common/service_discovery.h"
#include "common/error.h"
#include "common/logger.h"
#include "common/perf_logger.h"
#include "rank_sub.pb.h"

DEFINE_int32(rank_master_server_port, 8004, "RankMaster server listen port");
DEFINE_string(registry_backend, "discovery_server", "");
DEFINE_string(discovery_addr, "127.0.0.1:8100", "");
DEFINE_string(etcd_endpoints, "127.0.0.1:2379", "");
DECLARE_int32(discovery_refresh_interval_ms);
DEFINE_int32(sub_worker_timeout_ms, 5000, "Sub-worker call timeout (ms)");
DEFINE_string(sub_worker_service_type, "rank_sub", "");
DEFINE_string(sub_worker_connection_type, "pooled", "");
DEFINE_int32(sub_worker_max_retry, 3, "");
DEFINE_int32(sub_worker_connect_timeout_ms, -1, "");
DEFINE_int32(sub_worker_backup_request_ms, -1, "");
DEFINE_int32(rank_master_sleep_time_ms, 30, "");

namespace rank {

using namespace common::error;

RankMasterServiceImpl::RankMasterServiceImpl() {
    LOG_INFO << "RankMasterServiceImpl initialized";
    LOG_INFO << "Sub-worker service: " << FLAGS_sub_worker_service_type;
    LOG_INFO << "RankMaster sleep: " << FLAGS_rank_master_sleep_time_ms << " ms";

    std::string backend_addr = (FLAGS_registry_backend == "etcd")
        ? FLAGS_etcd_endpoints : FLAGS_discovery_addr;
    service_discovery_ = std::make_unique<common::ServiceDiscovery>(
        FLAGS_registry_backend, backend_addr, FLAGS_discovery_refresh_interval_ms);

    datasystem::ServiceDiscoveryOptions sdOpts;
    sdOpts.etcdAddress = FLAGS_etcd_endpoints;
    sdOpts.hostIdEnvName = "HOST_ID";
    sdOpts.affinityPolicy = datasystem::ServiceAffinityPolicy::PREFERRED_SAME_NODE;
    kv_service_discovery_ = std::make_shared<datasystem::ServiceDiscovery>(sdOpts);

    auto rc = kv_service_discovery_->Init();
    if (!rc.IsOk()) {
        LOG_ERROR << "KV ServiceDiscovery init failed: " << rc.ToString();
    }

    LOG_INFO << "Discovery backend=" << FLAGS_registry_backend << " addr=" << backend_addr;
}

void RankMasterServiceImpl::Rank(google::protobuf::RpcController* controller,
                                 const RankMasterRequest* request,
                                 RankMasterResponse* response,
                                 google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    if (!request->trace_id().empty()) {
        std::string tid = request->trace_id();
        common::logger::Logger::Instance().SetTraceIdGetter([tid]() { return tid; });
    }
    auto status = process_rank_request(request, response);
    if (status.IsError()) {
        response->set_error_code(static_cast<int32_t>(status.Code()));
        response->set_error_message(status.ToString());
    }
}

bool RankMasterServiceImpl::call_sub_worker(
    const std::string& user_feat_data,
    const std::vector<uint64_t>& sku_ids,
    const std::string& trace_id,
    RankSubResponse* response) {

    std::string host;
    int port;
    std::string instance_id;
    if (!service_discovery_->GetInstance(FLAGS_sub_worker_service_type, host, port, instance_id)) {
        LOG_ERROR << "No sub-worker instance for " << FLAGS_sub_worker_service_type;
        return false;
    }

    std::string addr = host + ":" + std::to_string(port);
    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.timeout_ms = FLAGS_sub_worker_timeout_ms;
    opts.connection_type = FLAGS_sub_worker_connection_type.c_str();
    opts.max_retry = FLAGS_sub_worker_max_retry;
    if (FLAGS_sub_worker_connect_timeout_ms >= 0) opts.connect_timeout_ms = FLAGS_sub_worker_connect_timeout_ms;
    if (FLAGS_sub_worker_backup_request_ms >= 0) opts.backup_request_ms = FLAGS_sub_worker_backup_request_ms;
    if (channel.Init(addr.c_str(), &opts) != 0) {
        service_discovery_->ReportFailure(instance_id);
        LOG_ERROR << "Failed to init sub-worker channel to " << addr;
        return false;
    }

    RankSubRequest req;
    for (uint64_t id : sku_ids) req.add_sku_ids(id);
    req.set_user_feat(user_feat_data.data(), user_feat_data.size());
    req.set_trace_id(trace_id);

    brpc::Controller cntl;
    RankSubService_Stub stub(&channel);
    int64_t start_us = butil::gettimeofday_us();
    stub.Rank(&cntl, &req, response, nullptr);
    int64_t cost_us = butil::gettimeofday_us() - start_us;

    if (cntl.Failed()) {
        service_discovery_->ReportFailure(instance_id);
        common::perf::Log("rank_master", "sub_worker_rpc", "processing", trace_id,
                          common::perf::UsToMs(cost_us), "error", "instance=" + instance_id);
        return false;
    }
    service_discovery_->ReportSuccess(instance_id);
    common::perf::Log("rank_master", "sub_worker_rpc", "processing", trace_id,
                      common::perf::UsToMs(cost_us), "ok", "instance=" + instance_id);
    LOG_INFO << "Sub-worker returned " << response->skus_score_size() << " scores"
             << " cost=" << cost_us / 1000.0 << " ms instance=" << instance_id;
    return true;
}

common::error::Status RankMasterServiceImpl::process_rank_request(
    const RankMasterRequest* request, RankMasterResponse* response) {

    int64_t start_us = butil::gettimeofday_us();

    if (request->user_feat_key().empty()) {
        return common::error::Status(rank_master_errors::EMPTY_USER_FEAT_KEY, "Empty user_feat_key");
    }
    if (request->sku_ids_size() == 0) {
        return common::error::Status(rank_master_errors::EMPTY_SKUS, "Empty sku_ids");
    }

    std::vector<uint64_t> sku_ids;
    for (int i = 0; i < request->sku_ids_size(); ++i) sku_ids.push_back(request->sku_ids(i));

    datasystem::ConnectOptions connectOptions;
    connectOptions.serviceDiscovery = kv_service_discovery_;
    datasystem::KVClient kv_client(connectOptions);

    auto kv_status = kv_client.Init();
    common::perf::Log("rank_master", "kv_init", "processing", request->trace_id(),
                      common::perf::UsToMs(butil::gettimeofday_us() - start_us),
                      kv_status.IsOk() ? "ok" : "error");
    if (!kv_status.IsOk()) {
        return common::error::Status(rank_master_errors::INTERNAL_ERROR, "KVClient init failed: " + kv_status.ToString());
    }

    datasystem::Optional<datasystem::Buffer> buffer;
    kv_status = kv_client.Get(request->user_feat_key(), buffer);
    common::perf::Log("rank_master", "kv_get", "processing", request->trace_id(),
                      common::perf::UsToMs(butil::gettimeofday_us() - start_us),
                      kv_status.IsOk() ? "ok" : "error", "key=" + request->user_feat_key());
    if (!kv_status.IsOk()) {
        return common::error::Status(rank_master_errors::INTERNAL_ERROR, "KVClient Get failed: " + kv_status.ToString());
    }

    std::string user_feat_data(reinterpret_cast<const char*>(buffer->ImmutableData()), buffer->GetSize());

    RankSubResponse sub_rsp;
    if (!call_sub_worker(user_feat_data, sku_ids, request->trace_id(), &sub_rsp)) {
        return common::error::Status(rank_master_errors::SUB_WORKER_CALL_FAILED, "Sub-worker failed");
    }

    for (int i = 0; i < sub_rsp.skus_id_size(); ++i) {
        response->add_candidates(sub_rsp.skus_id(i));
        response->add_scores(sub_rsp.skus_score(i));
    }

    if (FLAGS_rank_master_sleep_time_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_rank_master_sleep_time_ms));
    }

    int64_t cost = butil::gettimeofday_us() - start_us;
    LOG_INFO << "RankMaster done: skus=" << sku_ids.size() << " candidates=" << response->candidates_size() << " cost=" << cost / 1000.0 << " ms";
    common::perf::Log("rank_master", "rank_master_total", "processing", request->trace_id(),
                      common::perf::UsToMs(cost), "ok");
    return common::error::Status::OK();
}

} // namespace rank
