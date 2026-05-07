// 1. 对应的头文件
#include "rank_master_server.h"

// 2. 标准库头文件
#include <chrono>
#include <sstream>
#include <vector>
#include <random>
#include <cstring>
#include <memory>
#include <algorithm>
#include <functional>
#include <future>
#include <map>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/time.h>
#include <gflags/gflags.h>

// 5. 本项目内其他头文件
#include "common/global_thread_pool.h"
#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"
#include "common/error.h"
#include "common/sku_utils.h"
#include "rank_sub.pb.h"

DEFINE_int32(server_port, 8005, "服务器监听端口");
DEFINE_int32(sub_worker_count, 10, "子图数量");
DEFINE_string(sub_worker_addresses, "127.0.0.1:8006", "子图地址列表（逗号分隔）");
DEFINE_int32(top_k, 100, "返回前 K 个商品");
DEFINE_bool(enable_timing_stats, true, "是否启用详细时延统计");
DEFINE_int32(sub_worker_timeout_ms, 5000, "子图调用超时时间（毫秒）");

namespace rank {

using namespace common::error;
using common::parse_skus_from_string;
using common::skus_to_string;
using common::distribute_skus_by_hash;

constexpr int SCORE_SCALE = 100;

RankMasterServiceImpl::RankMasterServiceImpl()
    : sub_worker_channels_() {

    LOG(INFO) << "RankMasterServiceImpl initialized";
    LOG(INFO) << "Sub-worker count: " << FLAGS_sub_worker_count;
    LOG(INFO) << "Top-K: " << FLAGS_top_k;
    LOG(INFO) << "Sub-worker addresses: " << FLAGS_sub_worker_addresses;

    std::vector<std::string> addresses;
    std::stringstream ss(FLAGS_sub_worker_addresses);
    std::string addr;

    while (std::getline(ss, addr, ',')) {
        addr.erase(0, addr.find_first_not_of(" "));
        addr.erase(addr.find_last_not_of(" ") + 1);
        if (!addr.empty()) {
            addresses.push_back(addr);
        }
    }

    if (addresses.empty()) {
        LOG(ERROR) << common::error::Status(rank_master_errors::SUB_WORKER_CHANNEL_INVALID,
            "No sub-worker addresses provided").ToString();
        addresses.push_back("127.0.0.1:8006");
    }

    for (int i = 0; i < FLAGS_sub_worker_count; ++i) {
        const std::string& worker_addr = addresses[i % addresses.size()];

        auto channel = std::make_unique<brpc::Channel>();
        brpc::ChannelOptions opts;
        opts.timeout_ms = FLAGS_sub_worker_timeout_ms;
        opts.connection_type = "pooled";

        if (channel->Init(worker_addr.c_str(), &opts) == 0) {
            sub_worker_channels_.push_back(std::move(channel));
            LOG(INFO) << "Initialized channel to sub-worker " << i << ": " << worker_addr;
        } else {
            LOG(ERROR) << common::error::Status(rank_master_errors::SUB_WORKER_CHANNEL_INVALID,
                "Failed to initialize channel to sub-worker " + std::to_string(i) + ": " + worker_addr).ToString();
            sub_worker_channels_.push_back(std::move(channel));
        }
    }
}

RankMasterServiceImpl::~RankMasterServiceImpl() {
    LOG(INFO) << "RankMasterServiceImpl destroyed";
}

void RankMasterServiceImpl::Rank(google::protobuf::RpcController* controller,
                                 const RankMasterRequest* request,
                                 RankMasterResponse* response,
                                 google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);

    auto& pool = common::get_global_thread_pool();

    auto future = pool.submit([this, request]() {
        RankMasterResponse local_response;
        auto status = process_rank_request(request, &local_response);
        return std::make_pair(status, local_response);
    });

    try {
        auto result = future.get();
        response->CopyFrom(result.second);
        if (result.first.IsError()) {
            cntl->SetFailed(result.first.ToString());
        }
    } catch (const std::exception& e) {
        auto status = common::error::Status(rank_master_errors::INTERNAL_ERROR,
            "Thread pool task failed: " + std::string(e.what()));
        LOG(ERROR) << status.ToString();
        cntl->SetFailed(status.ToString());
    }
}

bool RankMasterServiceImpl::call_sub_worker(int worker_index,
                                           const std::string& user_feat_key,
                                           const std::vector<uint64_t>& sku_ids,
                                           RankSubResponse* response) {

    if (worker_index >= static_cast<int>(sub_worker_channels_.size()) ||
        !sub_worker_channels_[worker_index]) {
        LOG(ERROR) << common::error::Status(rank_master_errors::SUB_WORKER_CHANNEL_INVALID,
            "Invalid sub-worker index: " + std::to_string(worker_index)).ToString();
        return false;
    }

    RankSubRequest request;
    request.set_user_feat_key(user_feat_key);
    request.set_skus_sub(skus_to_string(sku_ids));

    brpc::Controller cntl;

    rank::RankSubService_Stub stub(sub_worker_channels_[worker_index].get());

    stub.Rank(&cntl, &request, response, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << common::error::Status(rank_master_errors::SUB_WORKER_CALL_FAILED,
            "Sub-worker " + std::to_string(worker_index) + " call failed: " + cntl.ErrorText()).ToString();
        return false;
    }

    LOG(INFO) << "Sub-worker " << worker_index << " returned "
              << response->skus_score_size() << " scores";

    return true;
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

    LOG(INFO) << "Selected top " << candidates.size() << " from "
              << all_scores.size() << " candidates";
}

common::error::Status RankMasterServiceImpl::validate_and_parse(
    const RankMasterRequest* request, std::vector<uint64_t>& sku_ids) {

    if (request->user_feat_key().empty()) {
        auto status = common::error::Status(rank_master_errors::EMPTY_USER_FEAT_KEY,
            "Empty user_feat_key in request");
        LOG(ERROR) << status.ToString();
        return status;
    }

    if (request->skus().empty()) {
        auto status = common::error::Status(rank_master_errors::EMPTY_SKUS,
            "Empty skus in request");
        LOG(ERROR) << status.ToString();
        return status;
    }

    sku_ids = parse_skus_from_string(request->skus());

    if (sku_ids.empty()) {
        auto status = common::error::Status(rank_master_errors::NO_SKU_PARSED,
            "No SKU IDs parsed from skus");
        LOG(ERROR) << status.ToString();
        return status;
    }

    LOG(INFO) << "Total SKU count: " << sku_ids.size();
    return common::error::Status::OK();
}

common::error::Status RankMasterServiceImpl::call_workers_and_aggregate(
    const RankMasterRequest* request,
    const std::vector<uint64_t>& all_sku_ids,
    std::map<uint64_t, double>& all_scores) {

    auto distribution = distribute_skus_by_hash(all_sku_ids, FLAGS_sub_worker_count);

    std::vector<std::future<std::pair<int, RankSubResponse>>> futures;

    for (int i = 0; i < FLAGS_sub_worker_count; ++i) {
        if (distribution[i].empty()) {
            continue;
        }

        futures.push_back(std::async(std::launch::async, [this, i, &request, &distribution]() {
            RankSubResponse sub_response;
            bool success = call_sub_worker(i, request->user_feat_key(),
                                          distribution[i], &sub_response);
            return std::make_pair(success ? i : -1, sub_response);
        }));
    }

    int failed_workers = 0;

    for (auto& future : futures) {
        try {
            auto result = future.get();
            int worker_index = result.first;
            const RankSubResponse& sub_response = result.second;

            if (worker_index < 0) {
                LOG(WARN) << "Worker " << worker_index << " failed";
                ++failed_workers;
                continue;
            }

            for (int i = 0; i < sub_response.skus_id_size(); ++i) {
                uint64_t sku_id = sub_response.skus_id(i);
                uint64_t score_int = sub_response.skus_score(i);
                double score = static_cast<double>(score_int) / SCORE_SCALE;

                all_scores[sku_id] = score;
            }
        } catch (const std::exception& e) {
            ++failed_workers;
            LOG(ERROR) << common::error::Status(rank_master_errors::INTERNAL_ERROR,
                "Exception caught while collecting sub-worker result: " + std::string(e.what())).ToString();
        }
    }

    if (failed_workers > 0 && all_scores.empty()) {
        auto status = common::error::Status(rank_master_errors::SUB_WORKER_CALL_FAILED,
            "All " + std::to_string(failed_workers) + " sub-workers failed");
        LOG(ERROR) << status.ToString();
        return status;
    }

    if (failed_workers > 0) {
        LOG(WARN) << failed_workers << " sub-worker(s) failed, proceeding with partial results";
    }

    return common::error::Status::OK();
}

common::error::Status RankMasterServiceImpl::process_rank_request(const RankMasterRequest* request,
                                                 RankMasterResponse* response) {

    int64_t server_receive_us = butil::gettimeofday_us();

    LOG(INFO) << "RankMaster request received";

    std::vector<uint64_t> all_sku_ids;
    auto status = validate_and_parse(request, all_sku_ids);
    if (status.IsError()) {
        return status;
    }

    int64_t parallel_call_start_us = butil::gettimeofday_us();

    std::map<uint64_t, double> all_scores;
    status = call_workers_and_aggregate(request, all_sku_ids, all_scores);
    if (status.IsError()) {
        return status;
    }

    int64_t parallel_call_end_us = butil::gettimeofday_us();
    int64_t parallel_call_cost_us = parallel_call_end_us - parallel_call_start_us;

    LOG(INFO) << "Collected scores for " << all_scores.size() << " SKUs";

    int64_t select_topk_start_us = butil::gettimeofday_us();

    std::vector<uint64_t> candidates;
    select_top_k(all_scores, FLAGS_top_k, candidates);

    int64_t select_topk_end_us = butil::gettimeofday_us();
    int64_t select_topk_cost_us = select_topk_end_us - select_topk_start_us;

    for (uint64_t candidate : candidates) {
        response->add_candidates(candidate);
    }

    int64_t server_process_us = butil::gettimeofday_us() - server_receive_us;

    LOG(INFO) << "RankMaster processing completed:"
              << " input_skus=" << all_sku_ids.size()
              << " scored_skus=" << all_scores.size()
              << " output_candidates=" << response->candidates_size();

    if (FLAGS_enable_timing_stats) {
        LOG(INFO) << "Server timing breakdown:"
                  << " parallel_call_cost=" << parallel_call_cost_us / 1000.0 << " ms"
                  << " select_topk_cost=" << select_topk_cost_us / 1000.0 << " ms"
                  << " server_process_total=" << server_process_us / 1000.0 << " ms";
    }

    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;

    LOG(INFO) << "RankMaster completed, cost=" << cost_us / 1000.0 << " ms";

    return common::error::Status::OK();
}

} // namespace rank
