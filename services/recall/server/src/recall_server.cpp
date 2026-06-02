#include "recall_server.h"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <datasystem/kv_client.h>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "common/error.h"
#include "common/logger.h"

DEFINE_string(vllm_base_url, "http://127.0.0.1:8000", "vLLM 服务基础 URL");
DEFINE_string(vllm_endpoint, "/v1/chat/completions", "vLLM 聊天接口端点");
DEFINE_string(model_name, "/workspace/share/Qwen3-0.6B/", "模型名称");
DEFINE_int32(server_port, 8002, "服务器监听端口");
DEFINE_int32(vllm_timeout_ms, 100000, "vLLM 请求超时时间（毫秒）");
DEFINE_int32(sku_count, 1000, "返回的 SKU ID 数量（默认 1000）");
DEFINE_bool(enable_vllm, true, "是否启用 vLLM 生成式召回（false 时使用 KVCache 随机召回）");
DEFINE_string(kv_worker_service, "kv_worker", "KV Worker 服务名（用于 KVCache 召回）");
DEFINE_string(registry_backend, "discovery_server",
    "Registry backend: discovery_server or etcd");
DEFINE_string(discovery_addr, "127.0.0.1:8100", "Discovery server address");
DEFINE_string(etcd_endpoints, "127.0.0.1:2379",
    "etcd endpoints, comma-separated (for etcd backend)");
DEFINE_int32(kvcache_ttl_seconds, 3600, "KVCache TTL（秒，默认 1 小时）");
DEFINE_int32(kvcache_size_bytes, 256, "KVCache 大小（字节，作为 RNG seed blob）");
DEFINE_int32(recall_sleep_time_ms, 30, "Recall service simulated sleep time (ms)");

DEFINE_string(vllm_connection_type, "pooled",
              "vLLM channel connection type (pooled/short)");
DEFINE_int32(vllm_max_retry, 3,
             "vLLM channel BRPC max retry");
DEFINE_int32(vllm_connect_timeout_ms, -1,
             "vLLM channel connect timeout (ms), -1 = disabled");
DEFINE_int32(vllm_backup_request_ms, -1,
             "vLLM channel backup request (ms), -1 = disabled");


namespace recall {

using namespace common::error;

constexpr int VLLM_MAX_TOKENS = 1024;
constexpr double VLLM_TEMPERATURE = 0.7;
constexpr double VLLM_TOP_P = 0.9;

std::string proto_to_json(const RecallRequest* request) {
    using namespace rapidjson;

    Document d;
    d.SetObject();
    Document::AllocatorType& allocator = d.GetAllocator();

    d.AddMember("user_id", static_cast<uint64_t>(request->user_id()), allocator);

    Value user_logs(kArrayType);
    for (int i = 0; i < request->user_logs_size(); ++i) {
        const auto& log = request->user_logs(i);
        Value log_obj(kObjectType);

        Value vec(kArrayType);
        for (int j = 0; j < log.vec_size(); ++j) {
            vec.PushBack(log.vec(j), allocator);
        }

        log_obj.AddMember("vec", vec, allocator);
        user_logs.PushBack(log_obj, allocator);
    }
    d.AddMember("user_logs", user_logs, allocator);

    // d.AddMember("other", Value(request->other().c_str(), allocator).Move(), allocator);

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}

std::string build_vllm_request(const std::string& request_json) {
    using namespace rapidjson;

    Document d;
    d.SetObject();
    Document::AllocatorType& allocator = d.GetAllocator();

    d.AddMember("model", Value(FLAGS_model_name.c_str(), allocator).Move(), allocator);

    Value messages(kArrayType);

    Value system_msg(kObjectType);
    system_msg.AddMember("role", "system", allocator);
    std::ostringstream system_prompt_ss;
    system_prompt_ss << "You are a search/recommendation assistant. Based on user features and logs, "
                     << "return exactly " << FLAGS_sku_count << " recommended SKU IDs.\n"
                     << "Each SKU ID is a 64-bit unsigned integer in range [100000, 999999].\n"
                     << "CRITICAL: Output ONLY the comma-separated numbers. No explanation, no markdown, "
                     << "no extra text before or after. Format example: 123456,567890,111111,222222,...";
    system_msg.AddMember("content", Value(system_prompt_ss.str().c_str(), allocator).Move(), allocator);
    messages.PushBack(system_msg, allocator);

    Value user_msg(kObjectType);
    user_msg.AddMember("role", "user", allocator);

    std::ostringstream prompt_ss;
    prompt_ss << "User request data: " << request_json;
    user_msg.AddMember("content", Value(prompt_ss.str().c_str(), allocator).Move(), allocator);

    messages.PushBack(user_msg, allocator);
    d.AddMember("messages", messages, allocator);

    d.AddMember("max_tokens", VLLM_MAX_TOKENS, allocator);
    d.AddMember("temperature", VLLM_TEMPERATURE, allocator);
    d.AddMember("top_p", VLLM_TOP_P, allocator);
    d.AddMember("stream", false, allocator);

    if (FLAGS_sku_count > 0) {
        std::ostringstream regex_oss;
        regex_oss << "\\d{6}(,\\d{6}){" << (FLAGS_sku_count - 1) << "}";
        std::string regex_pattern = regex_oss.str();
        d.AddMember("guided_regex", Value(regex_pattern.c_str(), allocator).Move(), allocator);
        d.AddMember("guided_decoding_backend", "xgrammar", allocator);
    }

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}

bool parse_vllm_response(const std::string& response_body,
                        RecallResponse* response,
                        int max_sku_count) {
    using namespace rapidjson;

    Document d;
    d.Parse(response_body.c_str());

    if (d.HasParseError()) {
        LOG_ERROR << common::error::Status(recall_errors::VLLM_RESPONSE_PARSE_FAILED,
            "JSON parse error at offset: " + std::to_string(d.GetErrorOffset())).ToString();
        return false;
    }

    if (!d.HasMember("choices") || !d["choices"].IsArray() || d["choices"].Size() == 0) {
        LOG_ERROR << common::error::Status(recall_errors::VLLM_NO_CHOICES,
            "No choices in vLLM response").ToString();
        return false;
    }

    const Value& first_choice = d["choices"][0];

    if (!first_choice.HasMember("message") ||
        !first_choice["message"].HasMember("content")) {
        LOG_ERROR << common::error::Status(recall_errors::VLLM_NO_CONTENT,
            "No message content in vLLM response").ToString();
        return false;
    }

    std::string content = first_choice["message"]["content"].GetString();

    content = std::regex_replace(content, std::regex("[^0-9,]"), "");

    std::vector<uint64_t> parsed_ids;
    std::istringstream iss(content);
    std::string token;
    while (std::getline(iss, token, ',')) {
        try {
            token.erase(std::remove_if(token.begin(), token.end(),
                                       [](char c) { return std::isspace(c) || c == '"'; }),
                       token.end());

            if (!token.empty()) {
                uint64_t sku_id = std::stoull(token);
                parsed_ids.push_back(sku_id);
            }
        } catch (const std::exception& e) {
            LOG_WARN << "Failed to parse SKU ID: " << token << ", error: " << e.what();
        }
    }

    int raw_count = static_cast<int>(parsed_ids.size());
    if (raw_count == 0) {
        LOG_WARN << common::error::Status(recall_errors::NO_SKU_RETURNED,
            "No SKU IDs parsed from response").ToString();
        return false;
    }

    std::unordered_set<uint64_t> seen;
    std::vector<uint64_t> unique_ids;
    for (uint64_t id : parsed_ids) {
        if (seen.insert(id).second) {
            unique_ids.push_back(id);
        }
    }

    int unique_count = static_cast<int>(unique_ids.size());
    int dup_count = raw_count - unique_count;
    if (dup_count > 0) {
        LOG_INFO << "Deduplicated " << dup_count << " duplicate SKU IDs, "
                 << unique_count << " unique remaining";
    }

    if (unique_count < max_sku_count) {
        int need = max_sku_count - unique_count;
        LOG_INFO << "Filling " << need << " missing SKU IDs with random values";

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<uint64_t> dist(100000, 999999);

        for (int i = 0; i < need; ++i) {
            uint64_t new_id;
            do {
                new_id = dist(rng);
            } while (seen.count(new_id));
            seen.insert(new_id);
            unique_ids.push_back(new_id);
        }
    }

    std::shuffle(unique_ids.begin(), unique_ids.end(), std::mt19937(std::random_device{}()));

    for (uint64_t id : unique_ids) {
        response->add_sku_ids(id);
    }

    LOG_INFO << "Successfully parsed " << response->sku_ids_size()
              << " unique SKU IDs (raw=" << raw_count
              << ", target=" << max_sku_count << ")";

    return true;
}

// 从 kvcache blob 派生 RNG seed，生成 sku_count 个不重复 SKU ID
static void generate_skus_from_seed(const uint8_t* seed_data, size_t seed_size,
                                    int sku_count, RecallResponse* response) {
    // 将 seed blob 折叠为一个 64-bit seed
    uint64_t seed = 0;
    for (size_t i = 0; i < seed_size; ++i) {
        seed ^= static_cast<uint64_t>(seed_data[i]) << ((i % 8) * 8);
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> dist(100000, 999999);

    std::unordered_set<uint64_t> seen;
    while (static_cast<int>(seen.size()) < sku_count) {
        seen.insert(dist(rng));
    }

    for (uint64_t id : seen) {
        response->add_sku_ids(id);
    }
}

RecallServiceImpl::RecallServiceImpl()
    : vllm_client_(FLAGS_vllm_base_url, FLAGS_vllm_endpoint, FLAGS_vllm_timeout_ms) {
    LOG_INFO << "RecallServiceImpl initialized, enable_vllm=" << FLAGS_enable_vllm;
    LOG_INFO << "Recall sleep time: " << FLAGS_recall_sleep_time_ms << " ms";

    if (!FLAGS_enable_vllm) {
        datasystem::ServiceDiscoveryOptions sdOpts;
        sdOpts.etcdAddress = FLAGS_etcd_endpoints;
        sdOpts.hostIdEnvName = "HOST_ID";
        sdOpts.affinityPolicy = datasystem::ServiceAffinityPolicy::PREFERRED_SAME_NODE;
        service_discovery_ = std::make_shared<datasystem::ServiceDiscovery>(sdOpts);

        auto rc = service_discovery_->Init();
        if (!rc.IsOk()) {
            LOG_ERROR << "ServiceDiscovery init failed: " << rc.ToString();
        }

        LOG_INFO << "KVCache mode: KV Worker ServiceDiscovery initialized"
                  << ", etcd=" << FLAGS_etcd_endpoints;
    }
}

void RecallServiceImpl::Recall(google::protobuf::RpcController* controller,
                              const RecallRequest* request,
                              RecallResponse* response,
                              google::protobuf::Closure* done) {

    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);

    if (!request->trace_id().empty()) {
        std::string tid = request->trace_id();
        common::logger::Logger::Instance().SetTraceIdGetter([tid]() { return tid; });
    }

    LOG_INFO << "Recall request received, user_id: " << request->user_id()
              << ", log_count: " << request->user_logs_size()
              << ", remote=" << cntl->remote_side();

    auto result = process_recall_request(request);
    if (result.success) {
        response->CopyFrom(result.response);
        LOG_INFO << "Recall request processed successfully, user_id: "
                 << request->user_id()
                 << ", sku_count: " << response->sku_ids_size();
    } else {
        LOG_ERROR << result.error_message;
        response->set_error_code(static_cast<int32_t>(result.status.Code()));
        response->set_error_message(result.error_message);
    }
}

std::string RecallServiceImpl::derive_kvcache_key(const RecallRequest* request) {
    // 哈希 user_id + 所有 user_logs vec 值
    std::hash<std::string> hasher;
    std::ostringstream oss;
    oss << request->user_id();
    for (int i = 0; i < request->user_logs_size(); ++i) {
        const auto& log = request->user_logs(i);
        for (int j = 0; j < log.vec_size(); ++j) {
            oss << "," << log.vec(j);
        }
    }
    size_t hash_value = hasher(oss.str());

    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016zx", hash_value);
    return "rc:" + std::string(buf, 16);
}

RecallServiceImpl::RecallResult RecallServiceImpl::process_kvcache_recall(
    const RecallRequest* request) {

    RecallServiceImpl::RecallResult result;
    int64_t start_us = butil::gettimeofday_us();

    std::string kv_key = derive_kvcache_key(request);
    LOG_DEBUG << "KVCache key: " << kv_key;

    datasystem::ConnectOptions connectOptions;
    connectOptions.serviceDiscovery = service_discovery_;

    datasystem::KVClient kv_client(connectOptions);
    datasystem::Status kv_status = kv_client.Init();
    if (!kv_status.IsOk()) {
        result.success = false;
        result.status = common::error::Status(recall_errors::KVCLIENT_INIT_FAILED,
            "KVClient init failed: " + kv_status.ToString());
        result.error_message = result.status.ToString();
        return result;
    }

    // 尝试 Get
    datasystem::Optional<datasystem::Buffer> buffer;
    int64_t kv_get_start_us = butil::gettimeofday_us();
    kv_status = kv_client.Get(kv_key, buffer);
    int64_t kv_get_cost_us = butil::gettimeofday_us() - kv_get_start_us;

    if (kv_status.IsOk()) {
        // Cache HIT：用已有 kvcache 生成 SKU
        LOG_INFO << "KVCache HIT: key=" << kv_key
                  << ", size=" << buffer->GetSize() << " bytes"
                  << ", get_cost=" << kv_get_cost_us / 1000.0 << " ms";

        generate_skus_from_seed(
            reinterpret_cast<const uint8_t*>(buffer->ImmutableData()),
            buffer->GetSize(),
            FLAGS_sku_count,
            &result.response);
    } else {
        // Cache MISS：生成随机 kvcache，写入 KVWorker，再生成 SKU
        LOG_INFO << "KVCache MISS: key=" << kv_key
                  << ", get_cost=" << kv_get_cost_us / 1000.0 << " ms"
                  << ", generating new kvcache";

        int cache_size = FLAGS_kvcache_size_bytes;
        std::vector<uint8_t> seed_blob(cache_size);
        {
            std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> byte_dist(0, 255);
            for (auto& b : seed_blob) {
                b = static_cast<uint8_t>(byte_dist(rng));
            }
        }

        // 写入 KVWorker
        datasystem::WriteParam param;
        param.ttlSecond = FLAGS_kvcache_ttl_seconds;
        param.writeMode = datasystem::WriteMode::NONE_L2_CACHE;
        param.existence = datasystem::ExistenceOpt::NONE;
        param.cacheType = datasystem::CacheType::MEMORY;

        std::shared_ptr<datasystem::Buffer> write_buffer;
        int64_t create_start_us = butil::gettimeofday_us();
        kv_status = kv_client.Create(kv_key, cache_size, param, write_buffer);
        int64_t create_cost_us = butil::gettimeofday_us() - create_start_us;

        if (!kv_status.IsOk()) {
            result.success = false;
            result.status = common::error::Status(recall_errors::KVCLIENT_CREATE_FAILED,
                "KVClient Create failed: " + kv_status.ToString());
            result.error_message = result.status.ToString();
            return result;
        }

        std::memcpy(write_buffer->MutableData(), seed_blob.data(), cache_size);

        int64_t set_start_us = butil::gettimeofday_us();
        kv_status = kv_client.Set(write_buffer);
        int64_t set_cost_us = butil::gettimeofday_us() - set_start_us;

        if (!kv_status.IsOk()) {
            result.success = false;
            result.status = common::error::Status(recall_errors::KVCLIENT_SET_FAILED,
                "KVClient Set failed: " + kv_status.ToString());
            result.error_message = result.status.ToString();
            return result;
        }

        LOG_INFO << "KVCache written: key=" << kv_key
                  << ", size=" << cache_size << " bytes"
                  << ", ttl=" << FLAGS_kvcache_ttl_seconds << "s"
                  << ", create_cost=" << create_cost_us / 1000.0 << " ms"
                  << ", set_cost=" << set_cost_us / 1000.0 << " ms";

        generate_skus_from_seed(seed_blob.data(), seed_blob.size(),
                                FLAGS_sku_count, &result.response);
    }

    if (FLAGS_recall_sleep_time_ms > 0) {
        LOG_INFO << "Simulating recall sleep: " << FLAGS_recall_sleep_time_ms << " ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_recall_sleep_time_ms));
    }

    LOG_INFO << "KVCache recall completed, key=" << kv_key
              << ", sku_count=" << result.response.sku_ids_size()
              << ", total_cost=" << (butil::gettimeofday_us() - start_us) / 1000.0 << " ms";

    result.success = true;
    return result;
}

RecallServiceImpl::RecallResult RecallServiceImpl::process_recall_request(const RecallRequest* request) {
    RecallServiceImpl::RecallResult result;
    int64_t server_receive_us = butil::gettimeofday_us();

    if (request->user_id() == 0) {
        result.success = false;
        result.status = common::error::Status(recall_errors::EMPTY_USER_ID, "Empty user_id in request");
        result.error_message = result.status.ToString();
        return result;
    }

    if (!FLAGS_enable_vllm) {
        return process_kvcache_recall(request);
    }

    // vLLM 路径
    std::string request_json = proto_to_json(request);
    LOG_DEBUG << "Request JSON size: " << request_json.size() << " bytes";

    std::string vllm_json = build_vllm_request(request_json);
    LOG_DEBUG << "Built vLLM request, size: " << vllm_json.size() << " bytes";

    int64_t vllm_start_us = butil::gettimeofday_us();
    auto vllm_resp = vllm_client_.SendRequest(vllm_json);
    int64_t vllm_end_us = butil::gettimeofday_us();

    if (!vllm_resp.success) {
        result.success = false;
        result.status = vllm_resp.status;
        result.error_message = vllm_resp.status.ToString();
        return result;
    }

    LOG_DEBUG << "vLLM response size: " << vllm_resp.body.size() << " bytes";

    int64_t parse_start_us = butil::gettimeofday_us();
    if (!parse_vllm_response(vllm_resp.body, &result.response, FLAGS_sku_count)) {
        result.success = false;
        result.status = common::error::Status(recall_errors::VLLM_RESPONSE_PARSE_FAILED,
            "Failed to parse vLLM response");
        result.error_message = result.status.ToString();
        return result;
    }
    int64_t parse_end_us = butil::gettimeofday_us();

    if (FLAGS_recall_sleep_time_ms > 0) {
        LOG_INFO << "Simulating recall sleep: " << FLAGS_recall_sleep_time_ms << " ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_recall_sleep_time_ms));
    }

    int64_t server_process_us = butil::gettimeofday_us() - server_receive_us;

    LOG_INFO << "Recall completed, cost=" << server_process_us / 1000.0 << " ms";

    result.success = true;
    return result;
}

} // namespace recall
