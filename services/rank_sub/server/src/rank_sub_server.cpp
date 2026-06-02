#include "rank_sub_server.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <datasystem/kv_client.h>

#include "common/error.h"
#include "common/logger.h"
#include "common/sku_utils.h"

using namespace datasystem;

using namespace datasystem;

DEFINE_int32(server_port, 8005, "服务器监听端口");
DEFINE_string(registry_backend, "discovery_server",
    "Registry backend: discovery_server or etcd");
DEFINE_string(discovery_addr, "127.0.0.1:8100",
    "Discovery server address");
DEFINE_string(etcd_endpoints, "127.0.0.1:2379",
    "etcd endpoints, comma-separated (for etcd backend)");
DEFINE_string(kv_worker_service, "kv_worker",
    "KV Worker service name to discover");
DEFINE_int32(rank_sub_sleep_time_ms, 30, "RankSub service simulated sleep time (ms)");


namespace rank {

using namespace common::error;
using common::parse_skus_from_string;

constexpr int SCORE_RANGE = 10000;
constexpr int SCORE_SCALE = 100;

double simulate_score(uint64_t sku_id, const std::string& user_feat) {
    std::hash<std::string> hasher;
    size_t user_hash = hasher(user_feat);
    size_t sku_hash = std::hash<uint64_t>{}(sku_id);

    double score = static_cast<double>((user_hash ^ sku_hash) % SCORE_RANGE) / SCORE_SCALE;

    return score;
}

RankSubServiceImpl::RankSubServiceImpl() {
    datasystem::ServiceDiscoveryOptions sdOpts;
    sdOpts.etcdAddress = FLAGS_etcd_endpoints;
    sdOpts.hostIdEnvName = "HOST_ID";
    sdOpts.affinityPolicy = datasystem::ServiceAffinityPolicy::PREFERRED_SAME_NODE;
    service_discovery_ = std::make_shared<datasystem::ServiceDiscovery>(sdOpts);

    auto rc = service_discovery_->Init();
    if (!rc.IsOk()) {
        LOG_ERROR << "ServiceDiscovery init failed: " << rc.ToString();
    }

    LOG_INFO << "RankSubServiceImpl initialized";
    LOG_INFO << "KV Worker ServiceDiscovery: etcd=" << FLAGS_etcd_endpoints
              << ", affinity=PREFERRED_SAME_NODE";
    LOG_INFO << "RankSub sleep time: " << FLAGS_rank_sub_sleep_time_ms << " ms";
}

void RankSubServiceImpl::Rank(google::protobuf::RpcController* controller,
                              const RankSubRequest* request,
                              RankSubResponse* response,
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

common::error::Status RankSubServiceImpl::process_rank_request(const RankSubRequest* request,
                                              RankSubResponse* response) {

    int64_t server_receive_us = butil::gettimeofday_us();

    LOG_INFO << "Rank request received";

    if (request->user_feat_key().empty()) {
        auto status = common::error::Status(rank_sub_errors::EMPTY_USER_FEAT_KEY,
            "Empty user_feat_key in request");
        LOG_ERROR << status.ToString();
        return status;
    }

    if (request->skus_sub().empty()) {
        auto status = common::error::Status(rank_sub_errors::EMPTY_SKUS_SUB,
            "Empty skus_sub in request");
        LOG_ERROR << status.ToString();
        return status;
    }

    datasystem::ConnectOptions connectOptions;
    connectOptions.serviceDiscovery = service_discovery_;

    LOG_DEBUG << "Connecting to kv_worker via SDK ServiceDiscovery";

    KVClient kv_client(connectOptions);

    datasystem::Status kv_status = kv_client.Init();
    if (!kv_status.IsOk()) {
        auto status = common::error::Status(rank_sub_errors::KVCLIENT_INIT_FAILED,
            "KVClient init failed: " + kv_status.ToString());
        LOG_ERROR << status.ToString();
        return status;
    }

    int64_t kv_read_start_us = butil::gettimeofday_us();

    datasystem::Optional<datasystem::Buffer> buffer;
    kv_status = kv_client.Get(request->user_feat_key(), buffer);

    int64_t kv_read_end_us = butil::gettimeofday_us();
    int64_t kv_read_cost_us = kv_read_end_us - kv_read_start_us;

    if (!kv_status.IsOk()) {
        auto status = common::error::Status(rank_sub_errors::KVCLIENT_GET_FAILED,
            "KVClient Get failed for key: " + request->user_feat_key() + ", error: " + kv_status.ToString());
        LOG_ERROR << status.ToString();
        return status;
    }

    std::string user_feat(reinterpret_cast<const char*>(buffer->ImmutableData()), buffer->GetSize());

    LOG_DEBUG << "Retrieved user_feat from KVWorker: key="
              << request->user_feat_key()
              << ", size=" << user_feat.size() << " bytes";

    std::vector<uint64_t> sku_ids = parse_skus_from_string(request->skus_sub());

    if (sku_ids.empty()) {
        auto status = common::error::Status(rank_sub_errors::NO_SKU_PARSED,
            "No SKU IDs parsed from skus_sub");
        LOG_ERROR << status.ToString();
        return status;
    }

    int64_t scoring_start_us = butil::gettimeofday_us();

    for (uint32_t sku_id : sku_ids) {
        double score = simulate_score(sku_id, user_feat);

        response->add_skus_id(sku_id);
        response->add_skus_score(static_cast<uint64_t>(score * SCORE_SCALE));
    }

    int64_t scoring_end_us = butil::gettimeofday_us();
    int64_t scoring_cost_us = scoring_end_us - scoring_start_us;

    if (FLAGS_rank_sub_sleep_time_ms > 0) {
        LOG_INFO << "Simulating rank_sub sleep: " << FLAGS_rank_sub_sleep_time_ms << " ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_rank_sub_sleep_time_ms));
    }

    int64_t server_send_us = butil::gettimeofday_us();
    int64_t server_process_us = server_send_us - server_receive_us;

    LOG_INFO << "Rank processing completed:"
              << " sku_count=" << sku_ids.size()
              << ", response_skus_id_count=" << response->skus_id_size()
              << ", response_skus_score_count=" << response->skus_score_size();

    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;

    LOG_INFO << "Rank completed, cost=" << cost_us / 1000.0 << " ms";

    return common::error::Status::OK();
}

} // namespace rank
