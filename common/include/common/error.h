#ifndef COMMON_ERROR_H
#define COMMON_ERROR_H

// 错误码体系头文件
#include "internal/error/error_code.h"
#include "internal/error/status.h"

// 常用错误码别名
namespace common {
namespace error {

// 成功状态
constexpr Status OkStatus = Status::OK();

// 常用错误创建函数
inline Status InvalidArgumentError(const std::string& message = "") {
    return Status::Error(common_errors::INVALID_ARGUMENT, message);
}

inline Status ResourceError(const std::string& message = "") {
    return Status::Error(common_errors::OUT_OF_MEMORY, message);
}

inline Status NotFoundError(const std::string& message = "") {
    return Status::Error(common_errors::FILE_NOT_FOUND, message);
}

inline Status TimeoutError(const std::string& message = "") {
    return Status::Error(common_errors::TIMEOUT_ERROR, message);
}

inline Status NetworkError(const std::string& message = "") {
    return Status::Error(common_errors::NETWORK_ERROR, message);
}

inline Status InternalError(const std::string& message = "") {
    return Status::Error(common_errors::UNKNOWN_ERROR, message);
}

} // namespace error
} // namespace common

#endif // COMMON_ERROR_H