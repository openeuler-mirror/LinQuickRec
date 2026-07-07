#include "precalc_server.h"

#include <chrono>
#include <cstring>
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
#include "common/perf_logger.h"
#include "common/random_utils.h"

using namespace datasystem;

using namespace datasystem;

DEFINE_int32(server_port, 8003, "服务器监听端口");
DEFINE_string(registry_backend, "discovery_server",
    "Registry backend: discovery_server or etcd");
DEFINE_string(discovery_addr, "127.0.0.1:8100",
    "Discovery server address");
DEFINE_string(etcd_endpoints, "127.0.0.1:2379",
    "etcd endpoints, comma-separated (for etcd backend)");
DEFINE_string(kv_worker_service, "kv_worker",
    "KV Worker service name to discover");
DEFINE_double(precalc_result_size_mb, 8.5, "前置计算结果大小（MB），默认 8.5MB");
DEFINE_int32(ttl_seconds, 5, "TTL 时间（秒）");

DEFINE_int32(payload_size_kb, 100, "payload 大小（KB），默认 100KB");
DEFINE_int32(precalc_sleep_time_ms, 30, "Precalc service simulated sleep time (ms)");

namespace precalc {

using namespace common::error;

PrecalcServiceImpl::PrecalcServiceImpl() {
    LOG_INFO << "PrecalcServiceImpl initialized";
    LOG_INFO << "Precalc result size: " << FLAGS_precalc_result_size_mb << " MB";
    LOG_INFO << "TTL: " << FLAGS_ttl_seconds << " seconds";
    LOG_INFO << "Precalc sleep time: " << FLAGS_precalc_sleep_time_ms << " ms";

    datasystem::ServiceDiscoveryOptions sdOpts;
    sdOpts.etcdAddress = FLAGS_etcd_endpoints;
    sdOpts.hostIdEnvName = "HOST_ID";
    sdOpts.affinityPolicy = datasystem::ServiceAffinityPolicy::PREFERRED_SAME_NODE;
    service_discovery_ = std::make_shared<datasystem::ServiceDiscovery>(sdOpts);

    auto rc = service_discovery_->Init();
    if (!rc.IsOk()) {
        LOG_ERROR << "ServiceDiscovery init failed: " << rc.ToString();
        ready_ = false;
        return;
    }

    LOG_INFO << "KV Worker ServiceDiscovery: etcd=" << FLAGS_etcd_endpoints
              << ", affinity=PREFERRED_SAME_NODE";
}

void PrecalcServiceImpl::Precalculate(google::protobuf::RpcController* controller,
                                      const PrecalcRequest* request,
                                      PrecalcResponse* response,
                                      google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);

    if (!request->trace_id().empty()) {
        std::string tid = request->trace_id();
        common::logger::Logger::Instance().SetTraceIdGetter([tid]() { return tid; });
    }

    auto status = process_precalc_request(request, response);
    if (status.IsError()) {
        response->set_error_code(static_cast<int32_t>(status.Code()));
        response->set_error_message(status.ToString());
    }
}

std::string generate_user_feat_key(const std::string& trace_id, const std::string& user_feat) {
    // 优先使用 trace_id 后 16 字符（随机部分）
    if (trace_id.size() >= 16) {
        return trace_id.substr(trace_id.size() - 16, 16);
    }

    // 回退: 哈希 user_feat 并转为 16 字符 hex
    std::hash<std::string> hasher;
    size_t hash_value = hasher(user_feat);

    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016zx", hash_value);
    return std::string(buffer, 16);
}

common::error::Status PrecalcServiceImpl::validate_and_extract_key(
    const PrecalcRequest* request, std::string& user_feat_key) {

    if (request->user_feat().empty()) {
        auto status = common::error::Status(precalc_errors::EMPTY_USER_FEAT, "Empty user_feat in request");
        LOG_ERROR << status.ToString();
        return status;
    }

    user_feat_key = generate_user_feat_key(request->trace_id(), request->user_feat());

    LOG_DEBUG << "Generated user_feat_key: " << user_feat_key
              << ", size: " << user_feat_key.size() << " bytes"
              << ", trace_id: " << request->trace_id();

    return common::error::Status::OK();
}

common::error::Status PrecalcServiceImpl::write_to_kvworker(
    const std::string& user_feat_key,
    const std::string& precalc_result,
    const std::string& trace_id) {

    datasystem::ConnectOptions connectOptions;
    connectOptions.serviceDiscovery = service_discovery_;

    LOG_INFO << "KVWorker write start: key=" << user_feat_key
             << ", size=" << precalc_result.size() << " bytes";
    LOG_INFO << "KVClient Init start";

    KVClient kv_client(connectOptions);

    int64_t init_start_us = butil::gettimeofday_us();
    datasystem::Status kv_status = kv_client.Init();
    int64_t init_cost_us = butil::gettimeofday_us() - init_start_us;
    common::perf::Log("precalc", "kv_init", "processing", trace_id,
                      common::perf::UsToMs(init_cost_us),
                      kv_status.IsOk() ? "ok" : "error",
                      "key=" + user_feat_key);
    if (!kv_status.IsOk()) {
        auto status = common::error::Status(precalc_errors::KVCLIENT_INIT_FAILED,
            "KVClient init failed: " + kv_status.ToString());
        LOG_ERROR << status.ToString();
        return status;
    }
    LOG_INFO << "KVClient Init success, cost=" << init_cost_us / 1000.0 << " ms";

    SetParam param;
    param.ttlSecond = FLAGS_ttl_seconds;
    param.writeMode = WriteMode::NONE_L2_CACHE;
    param.existence = ExistenceOpt::NONE;
    param.cacheType = CacheType::MEMORY;

    std::shared_ptr<Buffer> buffer;
    LOG_INFO << "KVClient Create start: key=" << user_feat_key
             << ", size=" << precalc_result.size() << " bytes";
    int64_t create_start_us = butil::gettimeofday_us();
    kv_status = kv_client.Create(user_feat_key, precalc_result.size(), param, buffer);
    int64_t create_cost_us = butil::gettimeofday_us() - create_start_us;
    common::perf::Log("precalc", "kv_create", "processing", trace_id,
                      common::perf::UsToMs(create_cost_us),
                      kv_status.IsOk() ? "ok" : "error",
                      "key=" + user_feat_key);
    if (!kv_status.IsOk()) {
        auto status = common::error::Status(precalc_errors::KVCLIENT_CREATE_FAILED,
            "KVClient Create failed: " + kv_status.ToString());
        LOG_ERROR << status.ToString();
        return status;
    }
    LOG_INFO << "KVClient Create success, cost=" << create_cost_us / 1000.0 << " ms";

    int64_t memcpy_start_us = butil::gettimeofday_us();
    std::memcpy(buffer->MutableData(), precalc_result.data(), precalc_result.size());
    int64_t memcpy_cost_us = butil::gettimeofday_us() - memcpy_start_us;
    common::perf::Log("precalc", "kv_memcpy", "processing", trace_id,
                      common::perf::UsToMs(memcpy_cost_us), "ok",
                      "key=" + user_feat_key);
    LOG_INFO << "KVClient buffer memcpy completed, cost="
             << memcpy_cost_us / 1000.0 << " ms";

    LOG_INFO << "KVClient Set start: key=" << user_feat_key;
    int64_t set_start_us = butil::gettimeofday_us();
    kv_status = kv_client.Set(buffer);
    int64_t set_cost_us = butil::gettimeofday_us() - set_start_us;
    common::perf::Log("precalc", "kv_set", "processing", trace_id,
                      common::perf::UsToMs(set_cost_us),
                      kv_status.IsOk() ? "ok" : "error",
                      "key=" + user_feat_key);
    if (!kv_status.IsOk()) {
        auto status = common::error::Status(precalc_errors::KVCLIENT_SET_FAILED,
            "KVClient Set failed: " + kv_status.ToString());
        LOG_ERROR << status.ToString();
        return status;
    }
    LOG_INFO << "KVClient Set success, cost=" << set_cost_us / 1000.0 << " ms";

    LOG_INFO << "Precalc result written to KVWorker: key=" << user_feat_key
              << ", size=" << precalc_result.size() << " bytes ("
              << precalc_result.size() / (1024.0 * 1024.0) << " MB)";

    return common::error::Status::OK();
}

common::error::Status PrecalcServiceImpl::process_precalc_request(const PrecalcRequest* request,
                                                  PrecalcResponse* response) {

    int64_t server_receive_us = butil::gettimeofday_us();

    LOG_INFO << "Precalculate request received";

    std::string user_feat_key;
    auto status = validate_and_extract_key(request, user_feat_key);
    if (status.IsError()) {
        response->set_user_feat_key("");
        response->set_payload("");
        return status;
    }

    size_t precalc_size = static_cast<size_t>(FLAGS_precalc_result_size_mb * 1024 * 1024);
    LOG_INFO << "Generating precalc result: size=" << precalc_size << " bytes ("
             << FLAGS_precalc_result_size_mb << " MB)";
    int64_t generate_start_us = butil::gettimeofday_us();
    std::string precalc_result = common::generate_random_string(precalc_size);
    int64_t generate_cost_us = butil::gettimeofday_us() - generate_start_us;
    common::perf::Log("precalc", "generate_result", "processing", request->trace_id(),
                      common::perf::UsToMs(generate_cost_us), "ok",
                      "key=" + user_feat_key);
    LOG_INFO << "Generated precalc result, cost=" << generate_cost_us / 1000.0
             << " ms";

    int64_t kvwrite_start_us = butil::gettimeofday_us();

    status = write_to_kvworker(user_feat_key, precalc_result, request->trace_id());
    if (status.IsError()) {
        response->set_user_feat_key("");
        return status;
    }

    int64_t kvwrite_end_us = butil::gettimeofday_us();
    int64_t kvwrite_cost_us = kvwrite_end_us - kvwrite_start_us;
    common::perf::Log("precalc", "kv_write_total", "processing", request->trace_id(),
                      common::perf::UsToMs(kvwrite_cost_us), "ok",
                      "key=" + user_feat_key);

    int payload_size_kb = FLAGS_payload_size_kb > 0 ? FLAGS_payload_size_kb : 0;
    int64_t payload_start_us = butil::gettimeofday_us();
    std::string payload = common::generate_random_string(payload_size_kb * 1024);
    common::perf::Log("precalc", "generate_payload", "processing", request->trace_id(),
                      common::perf::UsToMs(butil::gettimeofday_us() - payload_start_us),
                      "ok", "payload_size=" + std::to_string(payload.size()));
    response->set_payload(payload);
    response->set_user_feat_key(user_feat_key);

    if (FLAGS_precalc_sleep_time_ms > 0) {
        LOG_INFO << "Simulating precalc sleep: " << FLAGS_precalc_sleep_time_ms << " ms";
        int64_t sleep_start_us = butil::gettimeofday_us();
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_precalc_sleep_time_ms));
        common::perf::Log("precalc", "sleep", "processing", request->trace_id(),
                          common::perf::UsToMs(butil::gettimeofday_us() - sleep_start_us));
    }

    int64_t server_process_us = butil::gettimeofday_us() - server_receive_us;

    LOG_INFO << "Precalculate success:"
              << " key=" << user_feat_key
              << ", key_size=" << user_feat_key.size() << " bytes"
              << ", payload_size=" << payload.size() << " bytes ("
              << payload.size() / 1024.0 << " KB)";

    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;

    LOG_INFO << "Precalculate completed, cost=" << cost_us / 1000.0 << " ms";
    common::perf::Log("precalc", "precalc_total", "processing", request->trace_id(),
                      common::perf::UsToMs(cost_us), "ok",
                      "key=" + user_feat_key);

    return common::error::Status::OK();
}

} // namespace precalc
