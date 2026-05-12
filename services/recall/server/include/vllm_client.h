#ifndef VLLM_CLIENT_H
#define VLLM_CLIENT_H

#include <string>

#include "common/error.h"

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
};

} // namespace recall

#endif // VLLM_CLIENT_H
