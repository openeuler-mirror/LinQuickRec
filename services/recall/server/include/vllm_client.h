#ifndef VLLM_CLIENT_H
#define VLLM_CLIENT_H

#include <string>

#include <brpc/channel.h>
#include <gflags/gflags.h>

#include "common/error.h"

DECLARE_string(vllm_connection_type);
DECLARE_int32(vllm_max_retry);
DECLARE_int32(vllm_connect_timeout_ms);
DECLARE_int32(vllm_backup_request_ms);

namespace recall {

struct VllmResponse {
    bool success = false;
    std::string body;
    common::error::Status status = common::error::Status::OK();
};

class VllmClient {
public:
    VllmClient(const std::string& base_url, const std::string& endpoint, int timeout_ms);

    VllmResponse SendRequest(const std::string& json_body);

private:
    std::string base_url_;
    std::string endpoint_;
    int timeout_ms_;
    brpc::Channel channel_;
};

} // namespace recall

#endif // VLLM_CLIENT_H
