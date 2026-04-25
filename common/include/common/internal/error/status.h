#ifndef COMMON_ERROR_STATUS_H
#define COMMON_ERROR_STATUS_H

#include "error_code.h"
#include <string>
#include <utility>

namespace common {
namespace error {

/**
 * @brief 错误状态封装类
 * 
 * 封装错误码和错误消息，提供便捷的错误处理接口
 */
class Status {
public:
    /**
     * @brief 默认构造函数，创建成功状态
     */
    Status() : code_(OK_CODE) {}
    
    /**
     * @brief 构造函数，使用错误码和消息
     * @param code 错误码
     * @param message 错误消息
     */
    Status(uint32_t code, std::string message = "")
        : code_(code), message_(std::move(message)) {}
    
    /**
     * @brief 创建成功状态
     */
    static Status OK() {
        return Status();
    }
    
    /**
     * @brief 创建错误状态
     * @param code 错误码
     * @param message 错误消息
     */
    static Status Error(uint32_t code, std::string message = "") {
        return Status(code, std::move(message));
    }
    
    /**
     * @brief 从模块、类型和具体错误码创建状态
     */
    static Status Error(ModuleCode module, ErrorType type, uint16_t specific_code,
                        std::string message = "") {
        return Status(MakeErrorCode(module, type, specific_code), std::move(message));
    }
    
    /**
     * @brief 检查状态是否成功
     */
    bool IsOk() const {
        return IsSuccessCode(code_);
    }
    
    /**
     * @brief 检查状态是否失败
     */
    bool IsError() const {
        return !IsOk();
    }
    
    /**
     * @brief 获取错误码
     */
    uint32_t Code() const {
        return code_;
    }
    
    /**
     * @brief 获取错误消息
     */
    const std::string& Message() const {
        return message_;
    }
    
    /**
     * @brief 设置错误消息
     */
    void SetMessage(std::string message) {
        message_ = std::move(message);
    }
    
    /**
     * @brief 获取模块代码
     */
    ModuleCode GetModule() const {
        return GetModuleFromCode(code_);
    }
    
    /**
     * @brief 获取错误类型
     */
    ErrorType GetType() const {
        return GetTypeFromCode(code_);
    }
    
    /**
     * @brief 获取具体错误码
     */
    uint16_t GetSpecificCode() const {
        return GetSpecificCodeFromCode(code_);
    }
    
    /**
     * @brief 转换为字符串表示
     */
    std::string ToString() const {
        if (IsOk()) {
            return "OK";
        }
        
        std::string result = ErrorCodeToString(code_);
        if (!message_.empty()) {
            result += ": " + message_;
        }
        return result;
    }
    
    /**
     * @brief 布尔转换，用于条件检查
     * @return true 如果状态成功
     */
    explicit operator bool() const {
        return IsOk();
    }
    
    /**
     * @brief 比较操作符
     */
    bool operator==(const Status& other) const {
        return code_ == other.code_ && message_ == other.message_;
    }
    
    bool operator!=(const Status& other) const {
        return !(*this == other);
    }
    
    /**
     * @brief 与成功状态比较
     */
    bool operator==(uint32_t code) const {
        return code_ == code;
    }
    
    bool operator!=(uint32_t code) const {
        return code_ != code;
    }
    
private:
    uint32_t code_;
    std::string message_;
};

} // namespace error
} // namespace common

#endif // COMMON_ERROR_STATUS_H