// 1. 对应的头文件
#include "recall_server.h"

// 2. 标准库头文件
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <random>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/server.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

// 5. 本项目内其他头文件
#include "common/global_thread_pool.h"

DEFINE_string(vllm_base_url, "http://127.0.0.1:8000", "vLLM 服务基础 URL");
DEFINE_string(vllm_endpoint, "/v1/chat/completions", "vLLM 聊天接口端点");
DEFINE_string(model_name, "/workspace/share/Qwen3-0.6B/", "模型名称");
DEFINE_int32(server_port, 8001, "服务器监听端口");
DEFINE_int32(vllm_timeout_ms, 5000, "vLLM 请求超时时间（毫秒）");
DEFINE_int32(sku_count, 1000, "返回的 SKU ID 数量（默认 1000）");

namespace recall {

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
    
    d.AddMember("other", Value(request->other().c_str(), allocator).Move(), allocator);
    
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
                     << "返回格式：用逗号分隔的数字，例如：12345,67890,11111,...";
    system_msg.AddMember("content", Value(system_prompt_ss.str().c_str(), allocator).Move(), allocator);
    messages.PushBack(system_msg, allocator);
    
    Value user_msg(kObjectType);
    user_msg.AddMember("role", "user", allocator);
    
    std::ostringstream prompt_ss;
    prompt_ss << "用户请求数据：" << request_json;
    user_msg.AddMember("content", Value(prompt_ss.str().c_str(), allocator).Move(), allocator);
    
    messages.PushBack(user_msg, allocator);
    d.AddMember("messages", messages, allocator);
    
    d.AddMember("max_tokens", 102400, allocator);
    d.AddMember("temperature", 0.7, allocator);
    d.AddMember("top_p", 0.9, allocator);
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
        LOG(ERROR) << "JSON parse error at offset: " << d.GetErrorOffset();
        return false;
    }
    
    if (!d.HasMember("choices") || !d["choices"].IsArray() || d["choices"].Size() == 0) {
        LOG(ERROR) << "No choices in response";
        return false;
    }
    
    const Value& first_choice = d["choices"][0];
    
    if (!first_choice.HasMember("message") || 
        !first_choice["message"].HasMember("content")) {
        LOG(ERROR) << "No message content in response";
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
            LOG(WARNING) << "Failed to parse SKU ID: " << token << ", error: " << e.what();
        }
    }
    
    if (response->sku_ids_size() == 0) {
        LOG(WARNING) << "No SKU IDs parsed from response content: " << content;
        return false;
    }
    
    LOG(INFO) << "Successfully parsed " << response->sku_ids_size() 
              << " SKU IDs from response (target: " << max_sku_count << ")";
    
    return true;
}

RecallServiceImpl::RecallServiceImpl() 
    : thread_pool_(common::get_global_thread_pool()) {
    LOG(INFO) << "RecallServiceImpl initialized with global thread pool size: " 
              << thread_pool_.size();
}

void RecallServiceImpl::Recall(google::protobuf::RpcController* controller,
                              const RecallRequest* request,
                              RecallResponse* response,
                              google::protobuf::Closure* done) {
    
    brpc::ClosureGuard done_guard(done);
    (void)controller;  // 显式忽略未使用的参数，消除警告
    
    LOG(INFO) << "Recall request received, user_id: " << request->user_id();

    try {
        auto future = thread_pool_.submit([this, request]() {
            return process_recall_request(request);
        });

        auto result = future.get();

        if (result.success) {
            response->CopyFrom(result.response);
            LOG(INFO) << "Recall request processed successfully, user_id: " 
                     << request->user_id() 
                     << ", sku_count: " << response->sku_ids_size();
        } else {
            LOG(ERROR) << "Recall request failed: " << result.error_message;
        }

    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception caught: " << e.what();
    }
}

RecallServiceImpl::RecallResult RecallServiceImpl::process_recall_request(const RecallRequest* request) {
    RecallServiceImpl::RecallResult result;

    std::string request_json = proto_to_json(request);
    LOG(INFO) << "Converted request to JSON: " << request_json;

    std::string vllm_json = build_vllm_request(request_json);
    LOG(INFO) << "Built vLLM request: " << vllm_json;

    brpc::Channel channel;
    brpc::ChannelOptions channel_opts;
    channel_opts.timeout_ms = FLAGS_vllm_timeout_ms;
    channel_opts.protocol = "http";

    std::string url = FLAGS_vllm_base_url + FLAGS_vllm_endpoint;
    if (channel.Init(url.c_str(), &channel_opts) != 0) {
        result.success = false;
        result.error_message = "Failed to initialize vLLM channel";
        return result;
    }

    brpc::Controller http_cntl;
    http_cntl.http_request().uri() = FLAGS_vllm_endpoint;
    http_cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
    http_cntl.http_request().set_content_type("application/json");
    http_cntl.http_request().SetHeader("Host", "127.0.0.1:8000");
    http_cntl.http_request().SetHeader("User-Agent", "RecallService/1.0");
    http_cntl.http_request().SetHeader("Connection", "close");
    http_cntl.request_attachment().append(vllm_json);

    channel.CallMethod(nullptr, &http_cntl, nullptr, nullptr, nullptr);

    if (http_cntl.Failed()) {
        result.success = false;
        result.error_message = "vLLM service error: " + http_cntl.ErrorText();
        return result;
    }

    const std::string& resp_body = http_cntl.response_attachment().to_string();
    LOG(INFO) << "vLLM response: " << resp_body;

    if (!parse_vllm_response(resp_body, &result.response, FLAGS_sku_count)) {
        result.success = false;
        result.error_message = "Failed to parse vLLM response";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace recall
