#include "recall_server.h"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cctype>
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
#include <type_traits>
#include <unordered_set>
#include <utility>
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
#include "common/perf_logger.h"
#include "common/random_utils.h"

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
DEFINE_double(kvcache_hit_rate, 0.5, "KVCache simulated hit rate in novllm mode [0.0, 1.0]");
DEFINE_int32(kvcache_hit_sleep_time_ms, 10, "KVCache hit simulated sleep time (ms) in novllm mode");
DEFINE_int32(kvcache_miss_sleep_time_ms, 100, "KVCache miss simulated sleep time (ms) in novllm mode");
DEFINE_int32(recall_sleep_time_ms, 30, "Recall service simulated sleep time (ms)");
DEFINE_int32(recall_payload_size_kb, 0, "Recall response payload size (KB)");

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
constexpr const char* KVCACHE_GLOBAL_KEY = "rc:novllm:global_seed";

template <typename T, typename = void>
struct HasIsOk : std::false_type {};

template <typename T>
struct HasIsOk<T, std::void_t<decltype(std::declval<T&>().IsOk())>> : std::true_type {};

template <typename T, typename = void>
struct HasToString : std::false_type {};

template <typename T>
struct HasToString<T, std::void_t<decltype(std::declval<T&>().ToString())>> : std::true_type {};

template <typename Client, typename = void>
struct HasExistKeyOnly : std::false_type {};

template <typename Client>
struct HasExistKeyOnly<Client,
    std::void_t<decltype(std::declval<Client&>().Exist(std::declval<const std::string&>()))>>
    : std::true_type {};

template <typename Client, typename = void>
struct HasExistBoolRef : std::false_type {};

template <typename Client>
struct HasExistBoolRef<Client,
    std::void_t<decltype(std::declval<Client&>().Exist(std::declval<const std::string&>(),
                                                       std::declval<bool&>()))>>
    : std::true_type {};

struct KvExistResult {
    bool ok = true;
    bool exists = false;
    std::string message;
};

template <typename T>
std::string status_to_string(const T& status) {
    if constexpr (HasToString<T>::value) {
        return status.ToString();
    } else {
        return "";
    }
}

template <typename Client>
KvExistResult kv_client_exist(Client& kv_client, const std::string& key) {
    if constexpr (HasExistKeyOnly<Client>::value) {
        auto ret = kv_client.Exist(key);
        using Ret = decltype(ret);
        if constexpr (std::is_same_v<Ret, bool>) {
            return {true, ret, ""};
        } else if constexpr (HasIsOk<Ret>::value) {
            return {true, ret.IsOk(), status_to_string(ret)};
        } else {
            static_assert(HasIsOk<Ret>::value || std::is_same_v<Ret, bool>,
                          "Unsupported KVClient::Exist(key) return type");
        }
    } else if constexpr (HasExistBoolRef<Client>::value) {
        bool exists = false;
        auto ret = kv_client.Exist(key, exists);
        using Ret = decltype(ret);
        if constexpr (std::is_same_v<Ret, bool>) {
            return {ret, exists, ""};
        } else if constexpr (HasIsOk<Ret>::value) {
            return {ret.IsOk() || !exists, exists, status_to_string(ret)};
        } else {
            static_assert(HasIsOk<Ret>::value || std::is_same_v<Ret, bool>,
                          "Unsupported KVClient::Exist(key, bool&) return type");
        }
    } else {
        static_assert(HasExistKeyOnly<Client>::value || HasExistBoolRef<Client>::value,
                      "Unsupported KVClient::Exist signature");
    }
}

static double normalized_kvcache_hit_rate() {
    return std::max(0.0, std::min(1.0, FLAGS_kvcache_hit_rate));
}

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

    d.AddMember("payload", Value(request->payload().c_str(), allocator).Move(), allocator);

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

static void generate_random_skus(int sku_count, RecallResponse* response) {
    std::mt19937_64 rng(std::random_device{}());
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
    LOG_INFO << "Recall payload size: " << FLAGS_recall_payload_size_kb << " KB";

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
        LOG_INFO << "KVCache simulation: hit_rate=" << normalized_kvcache_hit_rate()
                 << ", hit_sleep=" << FLAGS_kvcache_hit_sleep_time_ms << " ms"
                 << ", miss_sleep=" << FLAGS_kvcache_miss_sleep_time_ms << " ms";
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
                 << ", sku_count: " << response->sku_ids_size()
                 << ", payload_size: " << response->payload().size() << " bytes";
    } else {
        LOG_ERROR << result.error_message;
        response->set_error_code(static_cast<int32_t>(result.status.Code()));
        response->set_error_message(result.error_message);
    }
}

common::error::Status RecallServiceImpl::write_global_kvcache(
    datasystem::KVClient& kv_client,
    const std::vector<uint8_t>& value,
    const std::string& trace_id,
    const std::string& op_tag) {

    datasystem::SetParam param;
    param.ttlSecond = FLAGS_kvcache_ttl_seconds;
    param.writeMode = datasystem::WriteMode::NONE_L2_CACHE;
    param.existence = datasystem::ExistenceOpt::NONE;
    param.cacheType = datasystem::CacheType::MEMORY;

    std::shared_ptr<datasystem::Buffer> write_buffer;
    int64_t create_start_us = butil::gettimeofday_us();
    datasystem::Status kv_status = kv_client.Create(
        KVCACHE_GLOBAL_KEY, value.size(), param, write_buffer);
    int64_t create_cost_us = butil::gettimeofday_us() - create_start_us;
    common::perf::Log("recall", "kvcache_create", "processing", trace_id,
                      common::perf::UsToMs(create_cost_us),
                      kv_status.IsOk() ? "ok" : "error",
                      "key=" + std::string(KVCACHE_GLOBAL_KEY) + ",op=" + op_tag);
    if (!kv_status.IsOk()) {
        return common::error::Status(recall_errors::KVCLIENT_CREATE_FAILED,
            "KVClient Create failed: " + kv_status.ToString());
    }

    std::memcpy(write_buffer->MutableData(), value.data(), value.size());

    int64_t set_start_us = butil::gettimeofday_us();
    kv_status = kv_client.Set(write_buffer);
    int64_t set_cost_us = butil::gettimeofday_us() - set_start_us;
    common::perf::Log("recall", "kvcache_set", "processing", trace_id,
                      common::perf::UsToMs(set_cost_us),
                      kv_status.IsOk() ? "ok" : "error",
                      "key=" + std::string(KVCACHE_GLOBAL_KEY) + ",op=" + op_tag);
    if (!kv_status.IsOk()) {
        return common::error::Status(recall_errors::KVCLIENT_SET_FAILED,
            "KVClient Set failed: " + kv_status.ToString());
    }

    LOG_INFO << "KVCache global key written: key=" << KVCACHE_GLOBAL_KEY
             << ", size=" << value.size() << " bytes"
             << ", ttl=" << FLAGS_kvcache_ttl_seconds << "s"
             << ", op=" << op_tag
             << ", create_cost=" << create_cost_us / 1000.0 << " ms"
             << ", set_cost=" << set_cost_us / 1000.0 << " ms";
    return common::error::Status::OK();
}

common::error::Status RecallServiceImpl::ensure_global_kvcache(
    datasystem::KVClient& kv_client,
    const std::string& trace_id) {

    std::lock_guard<std::mutex> lock(global_kvcache_mutex_);
    if (global_kvcache_initialized_) {
        return common::error::Status::OK();
    }

    int cache_size = std::max(1, FLAGS_kvcache_size_bytes);
    std::vector<uint8_t> value(cache_size);
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (auto& b : value) {
        b = static_cast<uint8_t>(byte_dist(rng));
    }

    auto status = write_global_kvcache(kv_client, value, trace_id, "init");
    if (status.IsError()) {
        LOG_ERROR << "KVCache global init failed: " << status.ToString();
        return status;
    }

    global_kvcache_value_ = std::move(value);
    global_kvcache_initialized_ = true;
    LOG_INFO << "KVCache global init success: key=" << KVCACHE_GLOBAL_KEY;
    return common::error::Status::OK();
}

RecallServiceImpl::RecallResult RecallServiceImpl::process_kvcache_recall(
    const RecallRequest* request) {

    RecallServiceImpl::RecallResult result;
    int64_t start_us = butil::gettimeofday_us();

    datasystem::ConnectOptions connectOptions;
    connectOptions.serviceDiscovery = service_discovery_;

    datasystem::KVClient kv_client(connectOptions);
    int64_t init_start_us = butil::gettimeofday_us();
    datasystem::Status kv_status = kv_client.Init();
    common::perf::Log("recall", "kv_init", "processing", request->trace_id(),
                      common::perf::UsToMs(butil::gettimeofday_us() - init_start_us),
                      kv_status.IsOk() ? "ok" : "error");
    if (!kv_status.IsOk()) {
        result.success = false;
        result.status = common::error::Status(recall_errors::KVCLIENT_INIT_FAILED,
            "KVClient init failed: " + kv_status.ToString());
        result.error_message = result.status.ToString();
        return result;
    }

    auto init_status = ensure_global_kvcache(kv_client, request->trace_id());
    if (init_status.IsError()) {
        result.success = false;
        result.status = init_status;
        result.error_message = result.status.ToString();
        return result;
    }

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> hit_dist(0.0, 1.0);
    bool cache_hit = hit_dist(rng) < normalized_kvcache_hit_rate();
    int sleep_time_ms = cache_hit ? FLAGS_kvcache_hit_sleep_time_ms
                                  : FLAGS_kvcache_miss_sleep_time_ms;

    if (cache_hit) {
        int64_t exist_start_us = butil::gettimeofday_us();
        auto exist_result = kv_client_exist(kv_client, KVCACHE_GLOBAL_KEY);
        int64_t exist_cost_us = butil::gettimeofday_us() - exist_start_us;
        common::perf::Log("recall", "kvcache_exist", "processing", request->trace_id(),
                          common::perf::UsToMs(exist_cost_us),
                          exist_result.exists ? "hit" : "miss",
                          "key=" + std::string(KVCACHE_GLOBAL_KEY) + ",cache_hit=true");

        if (!exist_result.ok || !exist_result.exists) {
            result.success = false;
            result.status = common::error::Status(recall_errors::INTERNAL_ERROR,
                "KVClient Exist failed or global key missing: " + exist_result.message);
            result.error_message = result.status.ToString();
            return result;
        }

        datasystem::Optional<datasystem::Buffer> buffer;
        int64_t kv_get_start_us = butil::gettimeofday_us();
        kv_status = kv_client.Get(KVCACHE_GLOBAL_KEY, buffer);
        int64_t kv_get_cost_us = butil::gettimeofday_us() - kv_get_start_us;
        common::perf::Log("recall", "kvcache_get", "processing", request->trace_id(),
                          common::perf::UsToMs(kv_get_cost_us),
                          kv_status.IsOk() ? "ok" : "error",
                          "key=" + std::string(KVCACHE_GLOBAL_KEY) + ",cache_hit=true");

        if (!kv_status.IsOk()) {
            result.success = false;
            result.status = common::error::Status(recall_errors::INTERNAL_ERROR,
                "KVClient Get failed: " + kv_status.ToString());
            result.error_message = result.status.ToString();
            return result;
        }

        LOG_INFO << "KVCache simulated HIT: key=" << KVCACHE_GLOBAL_KEY
                 << ", size=" << buffer->GetSize() << " bytes"
                 << ", exist_cost=" << exist_cost_us / 1000.0 << " ms"
                 << ", get_cost=" << kv_get_cost_us / 1000.0 << " ms";
    } else {
        std::ostringstream miss_key_ss;
        miss_key_ss << "rc:novllm:miss:" << request->user_id() << ":"
                    << butil::gettimeofday_us() << ":" << rng();
        std::string miss_key = miss_key_ss.str();

        int64_t exist_start_us = butil::gettimeofday_us();
        auto exist_result = kv_client_exist(kv_client, miss_key);
        int64_t exist_cost_us = butil::gettimeofday_us() - exist_start_us;
        common::perf::Log("recall", "kvcache_exist", "processing", request->trace_id(),
                          common::perf::UsToMs(exist_cost_us),
                          exist_result.exists ? "hit" : "miss",
                          "key=" + miss_key + ",cache_hit=false");

        if (!exist_result.ok) {
            result.success = false;
            result.status = common::error::Status(recall_errors::INTERNAL_ERROR,
                "KVClient Exist failed for miss probe: " + exist_result.message);
            result.error_message = result.status.ToString();
            return result;
        }

        std::vector<uint8_t> value;
        {
            std::lock_guard<std::mutex> lock(global_kvcache_mutex_);
            value = global_kvcache_value_;
        }
        auto write_status = write_global_kvcache(
            kv_client, value, request->trace_id(), "miss_rewrite");
        if (write_status.IsError()) {
            result.success = false;
            result.status = write_status;
            result.error_message = result.status.ToString();
            return result;
        }

        LOG_INFO << "KVCache simulated MISS: probe_key=" << miss_key
                 << ", probe_exists=" << (exist_result.exists ? "true" : "false")
                 << ", exist_cost=" << exist_cost_us / 1000.0 << " ms"
                 << ", rewritten_key=" << KVCACHE_GLOBAL_KEY;
    }

    generate_random_skus(FLAGS_sku_count, &result.response);

    int payload_size_kb = FLAGS_recall_payload_size_kb > 0 ? FLAGS_recall_payload_size_kb : 0;
    int64_t payload_start_us = butil::gettimeofday_us();
    result.response.set_payload(common::generate_random_string(payload_size_kb * 1024));
    common::perf::Log("recall", "generate_payload", "processing", request->trace_id(),
                      common::perf::UsToMs(butil::gettimeofday_us() - payload_start_us),
                      "ok", "payload_size=" + std::to_string(result.response.payload().size()));

    if (sleep_time_ms > 0) {
        LOG_INFO << "Simulating KVCache recall sleep: " << sleep_time_ms
                 << " ms, cache_hit=" << (cache_hit ? "true" : "false");
        int64_t sleep_start_us = butil::gettimeofday_us();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
        common::perf::Log("recall", "kvcache_sleep", "processing", request->trace_id(),
                          common::perf::UsToMs(butil::gettimeofday_us() - sleep_start_us),
                          "ok", std::string("cache_hit=") + (cache_hit ? "true" : "false"));
    }

    int64_t total_cost_us = butil::gettimeofday_us() - start_us;
    LOG_INFO << "KVCache recall completed, key=" << KVCACHE_GLOBAL_KEY
              << ", cache_hit=" << (cache_hit ? "true" : "false")
              << ", sku_count=" << result.response.sku_ids_size()
              << ", total_cost=" << total_cost_us / 1000.0 << " ms";
    common::perf::Log("recall", "recall_total", "processing", request->trace_id(),
                      common::perf::UsToMs(total_cost_us), "ok",
                      std::string("mode=kvcache,cache_hit=") + (cache_hit ? "true" : "false"));

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
    int64_t vllm_cost_us = vllm_end_us - vllm_start_us;
    common::perf::Log("recall", "vllm_rpc", "processing", request->trace_id(),
                      common::perf::UsToMs(vllm_cost_us),
                      vllm_resp.success ? "ok" : "error");
    common::perf::Log("recall", "recall_to_vllm_brpc", "brpc", request->trace_id(),
                      vllm_resp.brpc_latency_ms,
                      vllm_resp.success ? "ok" : "error");

    if (!vllm_resp.success) {
        result.success = false;
        result.status = vllm_resp.status;
        result.error_message = vllm_resp.status.ToString();
        return result;
    }

    LOG_DEBUG << "vLLM response size: " << vllm_resp.body.size() << " bytes";
    LOG_INFO << "vLLM request completed, cost=" << vllm_cost_us / 1000.0 << " ms";

    int64_t parse_start_us = butil::gettimeofday_us();
    if (!parse_vllm_response(vllm_resp.body, &result.response, FLAGS_sku_count)) {
        result.success = false;
        result.status = common::error::Status(recall_errors::VLLM_RESPONSE_PARSE_FAILED,
            "Failed to parse vLLM response");
        result.error_message = result.status.ToString();
        return result;
    }
    int64_t parse_end_us = butil::gettimeofday_us();
    int64_t parse_cost_us = parse_end_us - parse_start_us;
    common::perf::Log("recall", "vllm_parse", "processing", request->trace_id(),
                      common::perf::UsToMs(parse_cost_us), "ok");

    int payload_size_kb = FLAGS_recall_payload_size_kb > 0 ? FLAGS_recall_payload_size_kb : 0;
    int64_t payload_start_us = butil::gettimeofday_us();
    result.response.set_payload(common::generate_random_string(payload_size_kb * 1024));
    common::perf::Log("recall", "generate_payload", "processing", request->trace_id(),
                      common::perf::UsToMs(butil::gettimeofday_us() - payload_start_us),
                      "ok", "payload_size=" + std::to_string(result.response.payload().size()));

    if (FLAGS_recall_sleep_time_ms > 0) {
        LOG_INFO << "Simulating recall sleep: " << FLAGS_recall_sleep_time_ms << " ms";
        int64_t sleep_start_us = butil::gettimeofday_us();
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_recall_sleep_time_ms));
        common::perf::Log("recall", "sleep", "processing", request->trace_id(),
                          common::perf::UsToMs(butil::gettimeofday_us() - sleep_start_us));
    }

    int64_t server_process_us = butil::gettimeofday_us() - server_receive_us;

    LOG_INFO << "Recall completed, cost=" << server_process_us / 1000.0 << " ms"
              << ", vllm_cost=" << vllm_cost_us / 1000.0 << " ms"
              << ", parse_cost=" << parse_cost_us / 1000.0 << " ms";
    common::perf::Log("recall", "recall_total", "processing", request->trace_id(),
                      common::perf::UsToMs(server_process_us), "ok", "mode=vllm");

    result.success = true;
    return result;
}

} // namespace recall
