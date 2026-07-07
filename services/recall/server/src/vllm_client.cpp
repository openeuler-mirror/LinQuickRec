#include "vllm_client.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/time.h>

#include "common/error.h"
#include "common/logger.h"

namespace recall {

using namespace common::error;

VllmClient::VllmClient(const std::string& base_url, const std::string& endpoint, int timeout_ms)
    : base_url_(base_url), endpoint_(endpoint), timeout_ms_(timeout_ms) {

    brpc::ChannelOptions channel_opts;
    channel_opts.timeout_ms = timeout_ms_;
    channel_opts.protocol = "http";
    channel_opts.connection_type = FLAGS_vllm_connection_type.c_str();
    channel_opts.max_retry = FLAGS_vllm_max_retry;
    if (FLAGS_vllm_connect_timeout_ms >= 0) {
        channel_opts.connect_timeout_ms = FLAGS_vllm_connect_timeout_ms;
    }
    if (FLAGS_vllm_backup_request_ms >= 0) {
        channel_opts.backup_request_ms = FLAGS_vllm_backup_request_ms;
    }

    std::string url = base_url_ + endpoint_;
    if (channel_.Init(url.c_str(), &channel_opts) != 0) {
        LOG_ERROR << "Failed to initialize vLLM channel: " << url;
        channel_ready_ = false;
    } else {
        LOG_INFO << "vLLM channel initialized: " << url
                 << " (timeout=" << timeout_ms_ << "ms)";
    }
}

VllmResponse VllmClient::SendRequest(const std::string& json_body) {
    VllmResponse result;

    brpc::Controller cntl;
    cntl.http_request().uri() = endpoint_;
    cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
    cntl.http_request().set_content_type("application/json");
    cntl.http_request().SetHeader("Host", "127.0.0.1:8000");
    cntl.http_request().SetHeader("User-Agent", "RecallService/1.0");
    cntl.http_request().SetHeader("Connection", "close");
    cntl.request_attachment().append(json_body);

    int64_t start_us = butil::gettimeofday_us();

    channel_.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);

    int64_t end_us = butil::gettimeofday_us();
    double cost_ms = (end_us - start_us) / 1000.0;
    result.total_cost_ms = cost_ms;
    result.brpc_latency_ms = cntl.latency_us() / 1000.0;

    if (cntl.Failed()) {
        LOG_ERROR << "vLLM call failed: cost=" << cost_ms << " ms"
                   << ", error=" << cntl.ErrorText();
        result.status = common::error::Status(recall_errors::VLLM_REQUEST_FAILED,
            "vLLM service error: " + cntl.ErrorText());
        return result;
    }

    result.body = cntl.response_attachment().to_string();
    result.success = true;
    LOG_INFO << "vLLM call completed: cost=" << cost_ms << " ms"
              << ", status=" << cntl.http_response().status_code()
              << ", response_size=" << result.body.size() << " bytes";
    return result;
}

} // namespace recall
