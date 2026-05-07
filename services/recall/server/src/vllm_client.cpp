#include "vllm_client.h"

#include <brpc/channel.h>
#include <brpc/controller.h>

#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"
#include "common/error.h"

namespace recall {

using namespace common::error;

VllmClient::VllmClient(const std::string& base_url, const std::string& endpoint, int timeout_ms)
    : base_url_(base_url), endpoint_(endpoint), timeout_ms_(timeout_ms) {}

VllmResponse VllmClient::SendRequest(const std::string& json_body) {
    VllmResponse result;

    brpc::Channel channel;
    brpc::ChannelOptions channel_opts;
    channel_opts.timeout_ms = timeout_ms_;
    channel_opts.protocol = "http";

    std::string url = base_url_ + endpoint_;
    if (channel.Init(url.c_str(), &channel_opts) != 0) {
        result.status = common::error::Status(recall_errors::VLLM_CHANNEL_INIT_FAILED,
            "Failed to initialize vLLM channel");
        return result;
    }

    brpc::Controller cntl;
    cntl.http_request().uri() = endpoint_;
    cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
    cntl.http_request().set_content_type("application/json");
    cntl.http_request().SetHeader("Host", "127.0.0.1:8000");
    cntl.http_request().SetHeader("User-Agent", "RecallService/1.0");
    cntl.http_request().SetHeader("Connection", "close");
    cntl.request_attachment().append(json_body);

    channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);

    if (cntl.Failed()) {
        result.status = common::error::Status(recall_errors::VLLM_REQUEST_FAILED,
            "vLLM service error: " + cntl.ErrorText());
        return result;
    }

    result.body = cntl.response_attachment().to_string();
    result.success = true;
    return result;
}

} // namespace recall
