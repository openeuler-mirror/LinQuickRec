#include "recommend.pb.h"
#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>

#include <absl/strings/escaping.h>
#include <absl/strings/str_cat.h>

#include <gflags/gflags.h>

#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>

//vLLM 服务地址(第一阶段硬编码)
const char* VLLM_BASE_URL = "http://127.0.0.1:8000";
const char* VLLM_CHAT_ENDPOINT = "/v1/chat/completions";
const char* MODEL_NAME = "/workspace/share/Qwen3-0.6B";

// 简单的JOSN转义辅助函数（仅处理双引号和反斜杠，生产环境请用专业库）
std::string escape_json_string(const std::string& input) {
    std::ostringstream oss;
    for (char c : input) {
        unsigned char uc = static_cast<unsigned char>(c);
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"': oss << "\\\""; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (uc < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(uc);
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

// 简单的JSON字段提取辅助函数（极其简陋，仅适用于第一阶段固定格式解析）
// 在实际生产中，请务必使用 rapidjson 或 simdjson
bool extract_json_string(const std::string& json, const std::string& key, std::string& value) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return false;
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    
    size_t start = pos + 1;
    size_t end = start;
    while (end < json.size()) {
        if (json[end]=='"' && json[end-1]!='\\') break;
        end++;
    }
    
    value = json.substr(start,end-start);

    //反转义
    std::string unescaped;
    if (absl::CUnescape(value, &unescaped)) {
        value = unescaped;
        return true;
    }

    return true;
}

bool extract_json_int(const std::string& json, const std::string& key, int& value) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return false;
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    size_t end = pos; 
    while (end < json.size() && (isdigit(json[end]) || json[end] == '-')) end++;
    
    if (pos == end) return false;
    try {
        value = std::stoi(json.substr(pos, end - pos));
        return true;
    } catch (...) {
        return false;
    }
}

class RecommendServiceImpl : public recommend::RecommendService {
public:
    void Generate(google::protobuf::RpcController* cntl_base,
                  const recommend::GenerateRequest* request,
                  recommend::GenerateResponse* response,
                  google::protobuf::Closure* done) override {
                    
        brpc::ClosureGuard done_guard(done);
    //brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        //记录开始时间
        int64_t start_us = butil::gettimeofday_us();

        // 1. 构造发往 vLLM 的 HTTP 请求 Body (JSON)
        // 注意：这里手动拼接 JSON 字符串，生产环境中请使用 rapidjson
        std::string escaped_prompt = escape_json_string(request->prompt());
        std::ostringstream json_body;
        json_body << "{" 
                  << "\"model\": \"" << MODEL_NAME << "\"," 
                  << "\"messages\": [" 
                  << "{\"role\": \"user\", "
                  << "\"content\": \"" << escaped_prompt << "\"" 
                  << "}]," 
                  << "\"max_tokens\": " << request->max_tokens() << "," 
                  << "\"temperature\": " << request->temperature() << "," 
                  << "\"top_p\": " << request->top_p() << "," 
                  << "\"stream\": false" // 第一阶段先不支持流式
                  << "}";

    LOG(INFO) << "Sending vLLM JSON: " << json_body.str();

        // 2. 准备 BRPC Channel 和 Controller 用于调用 vLLM (HTTP)
        brpc::Channel channel;
        brpc::ChannelOptions channel_opts;
        channel_opts.timeout_ms = 5000; // vLLM 请求超时 5 秒
        channel_opts.protocol = "http"; // 强制使用 HTTP 协议

        std::string url = std::string(VLLM_BASE_URL) + VLLM_CHAT_ENDPOINT;
        if (channel.Init(url.c_str(), &channel_opts) != 0) {
            LOG(ERROR) << "Fail to initialize channel to vLLM:" << url;
            response->set_error_code(500);
            response->set_error_message("Internal error: Failed to connect to vLLM");
            return;
        }
        
        brpc::Controller http_cntl;
        http_cntl.http_request().uri() = VLLM_CHAT_ENDPOINT;
        http_cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
        http_cntl.http_request().set_content_type("application/json");
        http_cntl.http_request().set_header("Host", "127.0.0.1:8000");
        http_cntl.http_request().set_header("User-Agent", "curl/8.4.0");
        http_cntl.http_request().set_header("Connection", "close");
        http_cntl.request_attachment().append(json_body.str());

        // 3. 同步调用 vLLM
        // 注意：这里使用同步阻塞的，直到 vLLM 返回或超时
        // 对于第一阶段，这简化了逻辑，但会占用 BRPC 工作线程
        channel.CallMethod(NULL, &http_cntl, NULL, NULL, NULL);

        if (http_cntl.Failed()) {
            LOG(ERROR) << "vLLM request failed: " << http_cntl.ErrorText();
            response->set_error_code(502);
            response->set_error_message("vLLM service error: " + http_cntl.ErrorText());
            return;
        }

        // 4. 解析 vLLM 的 HTTP 响应 (JSON)
        const std::string& resp_body = http_cntl.response_attachment().to_string();

        // 简单解析(生产环境请替换为 rapidjson)
        std::string content;
        std::string finish_reason;
        int usage_total = 0, usage_prompt = 0, usage_completion = 0;

        // 尝试提取 choice[0].message.content
        // 由于手写解析器很弱，这里假设结构非常标准
        // 实际建议：if (!extract_nested_json(...))...
        // 这里为了演示，仅做最简单的逻辑示意
        // 真实场景中，强烈建议在此处引入 rapidjson

        // 模拟解析逻辑（实际代码需要更健壮的 JSON 解析）
        // 查找 “content”：“
        size_t c_pos = resp_body.find("\"content\":\"");  
        if (c_pos != std::string::npos) {
            size_t start = c_pos + 11;
            size_t end = start;
            while (end < resp_body.size()) {
                if (resp_body[end] == '"' && resp_body[end-1] != '\\') break;
                end++;
            }
            if (end > start) {
                content = resp_body.substr(start, end - start);
                std::string unescaped;
                if (absl::CUnescape(content, &unescaped)) {
                    content = unescaped;
                }
            }
        }

        //提取 finish_reason
        extract_json_string(resp_body, "finish_reason", finish_reason);

        // 提取 usage
        int tmp_val = 0;
        if (extract_json_int(resp_body, "total_tokens", tmp_val)) usage_total = tmp_val;
        if (extract_json_int(resp_body, "prompt_tokens", tmp_val)) usage_prompt = tmp_val;
        if (extract_json_int(resp_body, "completion_tokens", tmp_val)) usage_completion = tmp_val;
        
        //5. 填充响应
        if (content.empty()) {
            //如果解析失败，可能是 vLLM 返回了错误结构
            response->set_error_code(500);
            response->set_error_message("Failed to parse vLLM response or empty content");
            LOG(ERROR) << "Parse failed, raw response: " << resp_body;
        } else {
            response->set_generated_text(content);
            response->set_finish_reason(finish_reason);
            response->set_total_tokens(usage_total);
            response->set_prompt_tokens(usage_prompt);
            response->set_completion_tokens(usage_completion);
            response->set_error_code(0);
            response->set_error_message("");
        }

        
        // 计算延迟
        int64_t end_us = butil::gettimeofday_us();
        response->set_latency_ms((end_us - start_us)/1000);
        
        LOG(INFO) << "Request processed. Prompt len: " << request->prompt().size()
                  << ", Response len: " << content.size() 
                  << ", Latency: " << response->latency_ms() << "ms";
    }
};

int main(int argc, char* argv[]) {
    // 解析命令行参数
    google::ParseCommandLineFlags(&argc, &argv, true);
    
    brpc::Server server;
    RecommendServiceImpl service_impl;
    
    if (server.AddService(static_cast<google::protobuf::Service*>(&service_impl), brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add RecommendService";
        return -1;
    }
    
    // 监听 8001 端口 （避免与 vLLM 的 8000 冲突）
    if (server.Start("0.0.0.0:8001", nullptr) != 0) {
        LOG(ERROR) << "Failed to start server on port 8001";
        return -1;
    }
    
    LOG(INFO) << "Recommendation Gateway Server listening on 0.0.0.0:8001";
    LOG(INFO) << "vLLM expected at: " << VLLM_BASE_URL;
    
    server.RunUntilAskedToQuit();
    return 0;
}