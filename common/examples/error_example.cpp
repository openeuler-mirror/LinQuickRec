#include "common/error.h"
#include <iostream>

// Simulate a service function that returns Status
common::error::Status ProcessRequest(const std::string& user_id) {
    if (user_id.empty()) {
        return common::error::InvalidArgumentError("user_id cannot be empty");
    }
    if (user_id == "timeout") {
        return common::error::TimeoutError("upstream service timed out");
    }
    if (user_id == "notfound") {
        return common::error::NotFoundError("user not found in database");
    }
    if (user_id == "network") {
        return common::error::NetworkError("connection reset by peer");
    }
    if (user_id == "oom") {
        return common::error::ResourceError("out of memory allocating tensor");
    }
    return common::error::Status::OK();
}

void PrintErrorDetails(const common::error::Status& status) {
    std::cout << "  Status: " << status.ToString() << std::endl;
    std::cout << "  IsOk: " << status.IsOk() << std::endl;
    std::cout << "  Module: " << common::error::ModuleToString(status.GetModule())
              << " (0x" << std::hex << static_cast<int>(status.GetModule()) << ")"
              << std::dec << std::endl;
    std::cout << "  ErrorType: " << common::error::ErrorTypeToString(status.GetType())
              << " (0x" << std::hex << static_cast<int>(status.GetType()) << ")"
              << std::dec << std::endl;
    std::cout << "  SpecificCode: 0x" << std::hex << status.GetSpecificCode()
              << std::dec << std::endl;
    std::cout << "  Error Code: 0x" << std::hex << status.Code() << std::dec << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "=== Error Code System Examples ===" << std::endl;

    std::cout << "\n1. Successful request:" << std::endl;
    common::error::Status ok = ProcessRequest("valid_user");
    PrintErrorDetails(ok);
    if (ok) {
        std::cout << "  -> Request processed successfully" << std::endl;
    }

    std::cout << "2. Invalid argument error:" << std::endl;
    common::error::Status invalid = ProcessRequest("");
    PrintErrorDetails(invalid);
    if (!invalid) {
        std::cout << "  -> Request failed: " << invalid.Message() << std::endl;
    }

    std::cout << "3. Timeout error:" << std::endl;
    common::error::Status timeout = ProcessRequest("timeout");
    PrintErrorDetails(timeout);

    std::cout << "4. Not found error:" << std::endl;
    common::error::Status notfound = ProcessRequest("notfound");
    PrintErrorDetails(notfound);

    std::cout << "5. Network error:" << std::endl;
    common::error::Status network = ProcessRequest("network");
    PrintErrorDetails(network);

    std::cout << "6. Resource error (OOM):" << std::endl;
    common::error::Status oom = ProcessRequest("oom");
    PrintErrorDetails(oom);

    std::cout << "7. Module-specific error:" << std::endl;
    auto recall_err = common::error::Status::Error(
        common::error::ModuleCode::RECALL,
        common::error::ErrorType::SERVICE_ERROR,
        0x0001,
        "Recall service unavailable");
    PrintErrorDetails(recall_err);

    std::cout << "8. Common error constants:" << std::endl;
    std::cout << "  PERMISSION_DENIED = 0x" << std::hex
              << common::error::common_errors::PERMISSION_DENIED << std::dec << std::endl;
    std::cout << "  CONFIG_ERROR      = 0x" << std::hex
              << common::error::common_errors::CONFIG_ERROR << std::dec << std::endl;

    std::cout << "\n=== All Examples Completed ===" << std::endl;
    return 0;
}
