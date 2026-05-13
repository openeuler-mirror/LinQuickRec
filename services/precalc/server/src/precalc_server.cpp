#include "precalc_server.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <datasystem/kv_client.h>

#include "common/error.h"
#include "common/global_thread_pool.h"
#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"
#include "common/random_utils.h"

using namespace datasystem;

DEFINE_int32(server_port, 8003, "服务器监听端口");
DEFINE_string(kvworker_host, "141.61.84.245", "元戎 KVWorker 主机地址");
DEFINE_int32(kvworker_port, 31502, "元戎 KVWorker 端口 (PrecalcService)");
DEFINE_double(precalc_result_size_mb, 8.5, "前置计算结果大小（MB），默认 8.5MB");
DEFINE_int32(ttl_seconds, 5, "TTL 时间（秒）");

DEFINE_int32(payload_size_kb, 100, "payload 大小（KB），默认 100KB");

namespace precalc {

using namespace common::error;

PrecalcServiceImpl::PrecalcServiceImpl() {
    LOG_INFO << "PrecalcServiceImpl initialized";
    LOG_INFO << "Precalc result size: " << FLAGS_precalc_result_size_mb << " MB";
    LOG_INFO << "TTL: " << FLAGS_ttl_seconds << " seconds";
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

    auto& pool = common::get_global_thread_pool();

    auto future = pool.submit([this, request]() {
        PrecalcResponse local_response;
        auto status = process_precalc_request(request, &local_response);
        return std::make_pair(status, local_response);
    });

    try {
        auto result = future.get();
        response->CopyFrom(result.second);
        if (result.first.IsError()) {
            response->set_error_code(static_cast<int32_t>(result.first.Code()));
            response->set_error_message(result.first.ToString());
        }
    } catch (const std::exception& e) {
        auto status = common::error::Status(precalc_errors::INTERNAL_ERROR,
            "Thread pool task failed: " + std::string(e.what()));
        LOG_ERROR << status.ToString();
        response->set_error_code(static_cast<int32_t>(status.Code()));
        response->set_error_message(status.ToString());
    }
}

common::error::Status PrecalcServiceImpl::validate_and_extract_key(
    const PrecalcRequest* request, std::string& user_feat_key) {

    if (request->user_feat().empty()) {
        auto status = common::error::Status(precalc_errors::EMPTY_USER_FEAT, "Empty user_feat in request");
        LOG_ERROR << status.ToString();
        return status;
    }

    if (request->user_feat().size() >= 16) {
        user_feat_key = request->user_feat().substr(0, 16);
    } else {
        user_feat_key = request->user_feat();
    }

    LOG_DEBUG << "Generated user_feat_key: " << user_feat_key
              << ", size: " << user_feat_key.size() << " bytes"
              << ", user_feat_size: " << request->user_feat().size() << " bytes";

    return common::error::Status::OK();
}

common::error::Status PrecalcServiceImpl::write_to_kvworker(
    const std::string& user_feat_key, const std::string& precalc_result) {

    ConnectOptions connectOptions;
    connectOptions.host = FLAGS_kvworker_host;
    connectOptions.port = FLAGS_kvworker_port;

    KVClient kv_client(connectOptions);

    datasystem::Status kv_status = kv_client.Init();
    if (!kv_status.IsOk()) {
        auto status = common::error::Status(precalc_errors::KVCLIENT_INIT_FAILED,
            "KVClient init failed: " + kv_status.ToString());
        LOG_ERROR << status.ToString();
        return status;
    }

    SetParam param;
    param.ttlSecond = FLAGS_ttl_seconds;
    param.writeMode = WriteMode::NONE_L2_CACHE;
    param.existence = ExistenceOpt::NONE;
    param.cacheType = CacheType::MEMORY;

    std::shared_ptr<Buffer> buffer;
    kv_status = kv_client.Create(user_feat_key, precalc_result.size(), param, buffer);
    if (!kv_status.IsOk()) {
        auto status = common::error::Status(precalc_errors::KVCLIENT_CREATE_FAILED,
            "KVClient Create failed: " + kv_status.ToString());
        LOG_ERROR << status.ToString();
        return status;
    }

    std::memcpy(buffer->MutableData(), precalc_result.data(), precalc_result.size());

    kv_status = kv_client.Set(buffer);
    if (!kv_status.IsOk()) {
        auto status = common::error::Status(precalc_errors::KVCLIENT_SET_FAILED,
            "KVClient Set failed: " + kv_status.ToString());
        LOG_ERROR << status.ToString();
        return status;
    }

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
    std::string precalc_result = common::generate_random_string(precalc_size);
    LOG_DEBUG << "Generated precalc result with size: " << precalc_size << " bytes ("
              << FLAGS_precalc_result_size_mb << " MB)";

    int64_t kvwrite_start_us = butil::gettimeofday_us();

    status = write_to_kvworker(user_feat_key, precalc_result);
    if (status.IsError()) {
        response->set_user_feat_key("");
        return status;
    }

    int64_t kvwrite_end_us = butil::gettimeofday_us();
    int64_t kvwrite_cost_us = kvwrite_end_us - kvwrite_start_us;

    std::string payload = common::generate_random_string(FLAGS_payload_size_kb * 1024);
    response->set_payload(payload);
    response->set_user_feat_key(user_feat_key);

    int64_t server_process_us = butil::gettimeofday_us() - server_receive_us;

    LOG_INFO << "Precalculate success:"
              << " key=" << user_feat_key
              << ", key_size=" << user_feat_key.size() << " bytes"
              << ", payload_size=" << payload.size() << " bytes ("
              << payload.size() / 1024.0 << " KB)";

    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;

    LOG_INFO << "Precalculate completed, cost=" << cost_us / 1000.0 << " ms";

    return common::error::Status::OK();
}

} // namespace precalc
