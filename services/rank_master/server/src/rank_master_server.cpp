#include "rank_master_server.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <bthread/bthread.h>
#include <map>
#include <memory>
#include <random>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "common/discovery_naming_service.h"
#include "common/error.h"
#include "common/logger.h"
#include "common/sku_utils.h"
#include "rank_sub.pb.h"

DEFINE_int32(server_port, 8004, "服务器监听端口");
DEFINE_int32(discovery_refresh_interval_ms, 5000, "Discovery cache refresh interval (ms)");
DEFINE_int32(top_k, 100, "返回前 K 个商品");
DEFINE_int32(sub_worker_timeout_ms, 5000, "子图调用超时时间（毫秒）");
DEFINE_string(sub_worker_service_type, "rank_sub", "RankSub 在 Discovery 中注册的服务类型名");

DEFINE_string(sub_worker_connection_type, "pooled",
              "Sub-worker channel connection type (single/pooled/short)");
DEFINE_int32(sub_worker_max_retry, 3,
             "Sub-worker channel BRPC max retry");
DEFINE_int32(sub_worker_connect_timeout_ms, -1,
             "Sub-worker channel connect timeout (ms), -1 = disabled");
DEFINE_int32(sub_worker_backup_request_ms, -1,
             "Sub-worker channel backup request (ms), -1 = disabled");
DEFINE_int32(sub_worker_parallelism, 4,
             "Number of concurrent buckets when fanning out to RankSub");

namespace {

struct SubWorkerTask {
    rank::RankMasterServiceImpl* self;
    int bucket_index;
    const std::string* user_feat_key;
    const std::vector<uint64_t>* sku_ids;
    const std::string* trace_id;
    bool success = false;
    rank::RankSubResponse response;
};

} // namespace

namespace rank {

using namespace common::error;
using common::parse_skus_from_string;
using common::skus_to_string;
using common::distribute_skus_by_hash;

constexpr int SCORE_SCALE = 100;

RankMasterServiceImpl::RankMasterServiceImpl() {
    LOG_INFO << "RankMasterServiceImpl initialized";
    LOG_INFO << "Top-K: " << FLAGS_top_k;
    LOG_INFO << "Sub-worker service type: " << FLAGS_sub_worker_service_type;
    LOG_INFO << "Sub-worker parallelism: " << FLAGS_sub_worker_parallelism;

    sub_worker_channel_ = std::make_unique<brpc::Channel>();
    brpc::ChannelOptions opts;
    opts.timeout_ms = FLAGS_sub_worker_timeout_ms;
    opts.connection_type = FLAGS_sub_worker_connection_type.c_str();
    opts.max_retry = FLAGS_sub_worker_max_retry;
    if (FLAGS_sub_worker_connect_timeout_ms >= 0) {
        opts.connect_timeout_ms = FLAGS_sub_worker_connect_timeout_ms;
    }
    if (FLAGS_sub_worker_backup_request_ms >= 0) {
        opts.backup_request_ms = FLAGS_sub_worker_backup_request_ms;
    }

    std::string ns_url = "discovery://" + FLAGS_sub_worker_service_type;
    if (sub_worker_channel_->Init(ns_url.c_str(), &opts) != 0) {
        LOG_ERROR << "Failed to init sub-worker channel with " << ns_url;
    } else {
        LOG_INFO << "Sub-worker channel initialized: " << ns_url
                 << " (timeout=" << FLAGS_sub_worker_timeout_ms << "ms)";
    }
}

RankMasterServiceImpl::~RankMasterServiceImpl() {
    LOG_INFO << "RankMasterServiceImpl destroyed";
}

void RankMasterServiceImpl::Rank(google::protobuf::RpcController* controller,
                                 const RankMasterRequest* request,
                                 RankMasterResponse* response,
                                 google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);

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
    const std::string& user_feat_key,
    const std::vector<uint64_t>& sku_ids,
    const std::string& trace_id,
    RankSubResponse* response) {

    RankSubRequest request;
    request.set_user_feat_key(user_feat_key);
    request.set_skus_sub(skus_to_string(sku_ids));
    request.set_trace_id(trace_id);

    brpc::Controller cntl;
    rank::RankSubService_Stub stub(sub_worker_channel_.get());

    int64_t start_us = butil::gettimeofday_us();
    stub.Rank(&cntl, &request, response, nullptr);
    int64_t end_us = butil::gettimeofday_us();

    if (cntl.Failed()) {
        LOG_ERROR << common::error::Status(rank_master_errors::SUB_WORKER_CALL_FAILED,
            "Sub-worker call failed: " + cntl.ErrorText()).ToString();
        return false;
    }

    LOG_INFO << "Sub-worker returned " << response->skus_score_size() << " scores"
              << ", cost=" << (end_us - start_us) / 1000.0 << " ms"
              << ", brpc_latency=" << cntl.latency_us() / 1000.0 << " ms";

    return true;
}

void* RankMasterServiceImpl::sub_worker_bthread_fn(void* arg) {
    auto* task = static_cast<SubWorkerTask*>(arg);
    task->success = task->self->call_sub_worker(
        *task->user_feat_key, *task->sku_ids, *task->trace_id, &task->response);
    return nullptr;
}

void RankMasterServiceImpl::select_top_k(const std::map<uint64_t, double>& all_scores,
                                        int top_k,
                                        std::vector<uint64_t>& candidates) {

    std::vector<std::pair<uint64_t, double>> score_vec(all_scores.begin(), all_scores.end());

    if (static_cast<int>(score_vec.size()) <= top_k) {
        std::sort(score_vec.begin(), score_vec.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });

        for (const auto& p : score_vec) {
            candidates.push_back(p.first);
        }
    } else {
        std::nth_element(score_vec.begin(),
                        score_vec.begin() + top_k,
                        score_vec.end(),
                        [](const auto& a, const auto& b) {
                            return a.second > b.second;
                        });

        std::sort(score_vec.begin(),
                 score_vec.begin() + top_k,
                 [](const auto& a, const auto& b) { return a.second > b.second; });

        for (int i = 0; i < top_k; ++i) {
            candidates.push_back(score_vec[i].first);
        }
    }

    LOG_INFO << "Selected top " << candidates.size() << " from "
              << all_scores.size() << " candidates";
}

common::error::Status RankMasterServiceImpl::validate_and_parse(
    const RankMasterRequest* request, std::vector<uint64_t>& sku_ids) {

    if (request->user_feat_key().empty()) {
        auto status = common::error::Status(rank_master_errors::EMPTY_USER_FEAT_KEY,
            "Empty user_feat_key in request");
        LOG_ERROR << status.ToString();
        return status;
    }

    if (request->skus().empty()) {
        auto status = common::error::Status(rank_master_errors::EMPTY_SKUS,
            "Empty skus in request");
        LOG_ERROR << status.ToString();
        return status;
    }

    sku_ids = parse_skus_from_string(request->skus());

    if (sku_ids.empty()) {
        auto status = common::error::Status(rank_master_errors::NO_SKU_PARSED,
            "No SKU IDs parsed from skus");
        LOG_ERROR << status.ToString();
        return status;
    }

    LOG_DEBUG << "Total SKU count: " << sku_ids.size();
    return common::error::Status::OK();
}

common::error::Status RankMasterServiceImpl::call_workers_and_aggregate(
    const RankMasterRequest* request,
    const std::vector<uint64_t>& all_sku_ids,
    std::map<uint64_t, double>& all_scores,
    const std::string& trace_id) {

    int bucket_count = FLAGS_sub_worker_parallelism;
    if (bucket_count <= 0) {
        bucket_count = 4;
    }
    auto distribution = distribute_skus_by_hash(all_sku_ids, bucket_count);

    std::vector<SubWorkerTask> tasks;
    std::vector<bthread_t> tids;
    tasks.reserve(bucket_count);
    tids.reserve(bucket_count);

    for (int i = 0; i < bucket_count; ++i) {
        if (distribution[i].empty()) {
            continue;
        }

        tasks.push_back({this, i, &request->user_feat_key(), &distribution[i], &trace_id});

        bthread_t tid;
        if (bthread_start_background(&tid, nullptr, sub_worker_bthread_fn, &tasks.back()) == 0) {
            tids.push_back(tid);
        } else {
            LOG_ERROR << "Failed to start bthread for bucket " << i;
            tasks.back().success = false;
        }
    }

    for (bthread_t tid : tids) {
        bthread_join(tid, nullptr);
    }

    int failed_workers = 0;

    for (const auto& task : tasks) {
        if (!task.success) {
            LOG_WARN << "Bucket " << task.bucket_index << " failed";
            ++failed_workers;
            continue;
        }

        for (int i = 0; i < task.response.skus_id_size(); ++i) {
            uint64_t sku_id = task.response.skus_id(i);
            uint64_t score_int = task.response.skus_score(i);
            double score = static_cast<double>(score_int) / SCORE_SCALE;

            all_scores[sku_id] = score;
        }
    }

    if (failed_workers > 0 && all_scores.empty()) {
        auto status = common::error::Status(rank_master_errors::SUB_WORKER_CALL_FAILED,
            "All " + std::to_string(failed_workers) + " sub-worker calls failed");
        LOG_ERROR << status.ToString();
        return status;
    }

    if (failed_workers > 0) {
        LOG_WARN << failed_workers << " sub-worker call(s) failed, proceeding with partial results";
    }

    return common::error::Status::OK();
}

common::error::Status RankMasterServiceImpl::process_rank_request(const RankMasterRequest* request,
                                                 RankMasterResponse* response) {

    int64_t server_receive_us = butil::gettimeofday_us();

    LOG_INFO << "RankMaster request received";

    std::vector<uint64_t> all_sku_ids;
    auto status = validate_and_parse(request, all_sku_ids);
    if (status.IsError()) {
        return status;
    }

    std::map<uint64_t, double> all_scores;
    status = call_workers_and_aggregate(request, all_sku_ids, all_scores, request->trace_id());
    if (status.IsError()) {
        return status;
    }

    LOG_INFO << "Collected scores for " << all_scores.size() << " SKUs";

    std::vector<uint64_t> candidates;
    select_top_k(all_scores, FLAGS_top_k, candidates);

    for (uint64_t candidate : candidates) {
        response->add_candidates(candidate);
    }

    LOG_INFO << "RankMaster processing completed:"
              << " input_skus=" << all_sku_ids.size()
              << " scored_skus=" << all_scores.size()
              << " output_candidates=" << response->candidates_size();

    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;

    LOG_INFO << "RankMaster completed, cost=" << cost_us / 1000.0 << " ms";

    return common::error::Status::OK();
}

} // namespace rank
