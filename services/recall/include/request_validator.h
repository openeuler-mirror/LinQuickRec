#ifndef REQUEST_VALIDATOR_H
#define REQUEST_VALIDATOR_H

#include <string>
#include <limits>

/**
 * @brief 请求验证器
 * 
 * 用于验证 GenerateRequest 的合法性和安全性
 */
class RequestValidator {
public:
    /**
     * @brief 验证结果代码
     */
    enum class ValidationResult {
        VALID = 0,              // 验证通过
        INVALID_PROMPT,         // Prompt 无效
        PROMPT_TOO_LONG,        // Prompt 过长
        INVALID_MAX_TOKENS,     // max_tokens 无效
        INVALID_TEMPERATURE,    // temperature 无效
        INVALID_TOP_P,          // top_p 无效
        EMPTY_USER_ID           // user_id 为空
    };

    /**
     * @brief 默认配置
     */
    static constexpr size_t MAX_PROMPT_LENGTH = 4096;
    static constexpr int32_t MIN_MAX_TOKENS = 1;
    static constexpr int32_t MAX_MAX_TOKENS = 2048;
    static constexpr float MIN_TEMPERATURE = 0.0f;
    static constexpr float MAX_TEMPERATURE = 2.0f;
    static constexpr float MIN_TOP_P = 0.0f;
    static constexpr float MAX_TOP_P = 1.0f;

    /**
     * @brief 验证 prompt
     * 
     * @param prompt 要验证的 prompt
     * @param max_length 最大长度限制
     * @return ValidationResult 验证结果
     */
    static ValidationResult validate_prompt(const std::string& prompt,
                                           size_t max_length = MAX_PROMPT_LENGTH) {
        if (prompt.empty()) {
            return ValidationResult::INVALID_PROMPT;
        }
        
        if (prompt.length() > max_length) {
            return ValidationResult::PROMPT_TOO_LONG;
        }
        
        // 检查是否包含非法字符（可根据需要扩展）
        // 这里暂时只检查是否包含控制字符
        for (char c : prompt) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20 && uc != '\n' && uc != '\r' && uc != '\t') {
                return ValidationResult::INVALID_PROMPT;
            }
        }
        
        return ValidationResult::VALID;
    }

    /**
     * @brief 验证 max_tokens
     * 
     * @param max_tokens 要验证的 max_tokens 值
     * @return ValidationResult 验证结果
     */
    static ValidationResult validate_max_tokens(int32_t max_tokens) {
        if (max_tokens < MIN_MAX_TOKENS || max_tokens > MAX_MAX_TOKENS) {
            return ValidationResult::INVALID_MAX_TOKENS;
        }
        return ValidationResult::VALID;
    }

    /**
     * @brief 验证 temperature
     * 
     * @param temperature 要验证的 temperature 值
     * @return ValidationResult 验证结果
     */
    static ValidationResult validate_temperature(float temperature) {
        // 检查 NaN 和 Inf
        if (std::isnan(temperature) || std::isinf(temperature)) {
            return ValidationResult::INVALID_TEMPERATURE;
        }
        
        if (temperature < MIN_TEMPERATURE || temperature > MAX_TEMPERATURE) {
            return ValidationResult::INVALID_TEMPERATURE;
        }
        
        return ValidationResult::VALID;
    }

    /**
     * @brief 验证 top_p
     * 
     * @param top_p 要验证的 top_p 值
     * @return ValidationResult 验证结果
     */
    static ValidationResult validate_top_p(float top_p) {
        // 检查 NaN 和 Inf
        if (std::isnan(top_p) || std::isinf(top_p)) {
            return ValidationResult::INVALID_TOP_P;
        }
        
        if (top_p < MIN_TOP_P || top_p > MAX_TOP_P) {
            return ValidationResult::INVALID_TOP_P;
        }
        
        return ValidationResult::VALID;
    }

    /**
     * @brief 验证 user_id（可选）
     * 
     * @param user_id 要验证的 user_id
     * @param allow_empty 是否允许为空
     * @return ValidationResult 验证结果
     */
    static ValidationResult validate_user_id(const std::string& user_id,
                                            bool allow_empty = true) {
        if (!allow_empty && user_id.empty()) {
            return ValidationResult::EMPTY_USER_ID;
        }
        
        // 可以添加更多的 user_id 验证逻辑
        // 例如：长度限制、字符合法性检查等
        
        return ValidationResult::VALID;
    }

    /**
     * @brief 将验证结果转换为错误消息
     * 
     * @param result 验证结果
     * @return std::string 错误消息
     */
    static std::string validation_result_to_message(ValidationResult result) {
        switch (result) {
            case ValidationResult::VALID:
                return "Validation passed";
            case ValidationResult::INVALID_PROMPT:
                return "Invalid prompt: prompt is empty or contains invalid characters";
            case ValidationResult::PROMPT_TOO_LONG:
                return "Prompt too long: exceeds maximum length limit";
            case ValidationResult::INVALID_MAX_TOKENS:
                return "Invalid max_tokens: must be between 1 and 2048";
            case ValidationResult::INVALID_TEMPERATURE:
                return "Invalid temperature: must be between 0.0 and 2.0";
            case ValidationResult::INVALID_TOP_P:
                return "Invalid top_p: must be between 0.0 and 1.0";
            case ValidationResult::EMPTY_USER_ID:
                return "Empty user_id is not allowed";
            default:
                return "Unknown validation error";
        }
    }
};

#endif // REQUEST_VALIDATOR_H
