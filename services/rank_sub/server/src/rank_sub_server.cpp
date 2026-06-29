#include "rank_sub_server.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "common/error.h"
#include "common/logger.h"
#include "common/perf_logger.h"

DEFINE_int32(server_port, 8005, "Server listen port");
DEFINE_int32(rank_sub_sleep_time_ms, 30, "RankSub simulated sleep (ms)");

namespace rank {

using namespace common::error;

constexpr int SCORE_RANGE = 10000;
constexpr int SCORE_SCALE = 100;

double simulate_score(uint64_t sku_id, const std::string& user_feat) {
    size_t user_hash = std::hash<std::string>{}(user_feat);
    size_t sku_hash = std::hash<uint64_t>{}(sku_id);
    return static_cast<double>((user_hash ^ sku_hash) % SCORE_RANGE) / SCORE_SCALE;
}

RankSubServiceImpl::RankSubServiceImpl() {
    LOG_INFO << "RankSubServiceImpl initialized";
    LOG_INFO << "RankSub sleep: " << FLAGS_rank_sub_sleep_time_ms << " ms";
}

void RankSubServiceImpl::Rank(google::protobuf::RpcController* controller,
                              const RankSubRequest* request,
                              RankSubResponse* response,
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

common::error::Status RankSubServiceImpl::process_rank_request(
    const RankSubRequest* request, RankSubResponse* response) {

    int64_t start_us = butil::gettimeofday_us();

    if (request->user_feat().empty()) {
        return common::error::Status(rank_sub_errors::EMPTY_USER_FEAT_KEY, "Empty user_feat");
    }
    if (request->sku_ids_size() == 0) {
        return common::error::Status(rank_sub_errors::EMPTY_SKUS_SUB, "Empty sku_ids");
    }

    std::string user_feat = request->user_feat();

    std::vector<std::pair<uint64_t, uint64_t>> scored;
    for (int i = 0; i < request->sku_ids_size(); ++i) {
        uint64_t sku_id = request->sku_ids(i);
        double score = simulate_score(sku_id, user_feat);
        uint64_t score_int = static_cast<uint64_t>(score * SCORE_SCALE);
        scored.push_back({sku_id, score_int});
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& p : scored) {
        response->add_skus_id(static_cast<uint32_t>(p.first));
        response->add_skus_score(p.second);
    }

    int64_t score_cost = butil::gettimeofday_us() - start_us;
    common::perf::Log("rank_sub", "score_skus", "processing", request->trace_id(),
                      common::perf::UsToMs(score_cost), "ok",
                      "sku_count=" + std::to_string(scored.size()));

    if (FLAGS_rank_sub_sleep_time_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_rank_sub_sleep_time_ms));
    }

    int64_t cost = butil::gettimeofday_us() - start_us;
    LOG_INFO << "RankSub done: sku_count=" << scored.size() << " cost=" << cost / 1000.0 << " ms";
    common::perf::Log("rank_sub", "rank_sub_total", "processing", request->trace_id(),
                      common::perf::UsToMs(cost), "ok");
    return common::error::Status::OK();
}

} // namespace rank
