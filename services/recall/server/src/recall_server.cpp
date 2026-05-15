#include "recall_server.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

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
DEFINE_int32(sku_count, 100, "返回的 SKU ID 数量（默认 100）");

DEFINE_string(vllm_connection_type, "single",
              "vLLM channel connection type (single/pooled/short)");
DEFINE_int32(vllm_max_retry, 3,
             "vLLM channel BRPC max retry");
DEFINE_int32(vllm_connect_timeout_ms, -1,
             "vLLM channel connect timeout (ms), -1 = disabled");
DEFINE_int32(vllm_backup_request_ms, -1,
             "vLLM channel backup request (ms), -1 = disabled");


namespace recall {

using namespace common::error;

constexpr int VLLM_MAX_TOKENS = 10240;
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
    system_prompt_ss << "你是一个搜推广助手，请根据用户特征和日志返回推荐的 SKU ID 列表。\n"
                     << "请恰好生成 " << FLAGS_sku_count << " 个 SKU ID，不要多也不要少。\n"
                     << "每个SKU ID 都是一个 64 位无符号整数，范围在 100000 到 999999 之间。\n"
                     << "返回格式：用逗号分隔的数字，例如：123456,567890,111111,...";
    system_msg.AddMember("content", Value(system_prompt_ss.str().c_str(), allocator).Move(), allocator);
    messages.PushBack(system_msg, allocator);

    Value user_msg(kObjectType);
    user_msg.AddMember("role", "user", allocator);

    std::ostringstream prompt_ss;
    prompt_ss << "用户请求数据：" << request_json;
    user_msg.AddMember("content", Value(prompt_ss.str().c_str(), allocator).Move(), allocator);

    messages.PushBack(user_msg, allocator);
    d.AddMember("messages", messages, allocator);

    d.AddMember("max_tokens", VLLM_MAX_TOKENS, allocator);
    d.AddMember("temperature", VLLM_TEMPERATURE, allocator);
    d.AddMember("top_p", VLLM_TOP_P, allocator);
    d.AddMember("stream", false, allocator);

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

    const std::string content = first_choice["message"]["content"].GetString();

    std::istringstream iss(content);
    std::string token;
    while (std::getline(iss, token, ',')) {
        try {
            token.erase(std::remove_if(token.begin(), token.end(),
                                       [](char c) { return std::isspace(c) || c == '"'; }),
                       token.end());

            if (!token.empty()) {
                uint64_t sku_id = std::stoull(token);
                response->add_sku_ids(sku_id);
            }
        } catch (const std::exception& e) {
            LOG_WARN << "Failed to parse SKU ID: " << token << ", error: " << e.what();
        }
    }

    if (response->sku_ids_size() == 0) {
        LOG_WARN << common::error::Status(recall_errors::NO_SKU_RETURNED,
            "No SKU IDs parsed from response").ToString();
        return false;
    }

    LOG_INFO << "Successfully parsed " << response->sku_ids_size()
              << " SKU IDs from response (target: " << max_sku_count << ")";

    return true;
}

RecallServiceImpl::RecallServiceImpl()
    : vllm_client_(FLAGS_vllm_base_url, FLAGS_vllm_endpoint, FLAGS_vllm_timeout_ms) {
    LOG_INFO << "RecallServiceImpl initialized";
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

RecallServiceImpl::RecallResult RecallServiceImpl::process_recall_request(const RecallRequest* request) {
    RecallServiceImpl::RecallResult result;
    int64_t server_receive_us = butil::gettimeofday_us();

    if (request->user_id() == 0) {
        result.success = false;
        result.status = common::error::Status(recall_errors::EMPTY_USER_ID, "Empty user_id in request");
        result.error_message = result.status.ToString();
        return result;
    }

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

    int64_t server_process_us = butil::gettimeofday_us() - server_receive_us;

    LOG_INFO << "Recall completed, cost=" << server_process_us / 1000.0 << " ms";

    result.success = true;
    return result;
}

} // namespace recall
