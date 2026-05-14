#ifndef COMMON_ERROR_ERROR_CODE_H
#define COMMON_ERROR_ERROR_CODE_H

#include <cstdint>
#include <string>

namespace common {
namespace error {

// 错误码结构：0x MM TT CC CC
// MM: 模块代码 (8 bits)
// TT: 错误类型 (8 bits) 
// CC CC: 具体错误码 (16 bits)

/**
 * @brief 模块代码枚举
 */
enum class ModuleCode : uint8_t {
    COMMON     = 0x00,
    GATEWAY    = 0x01,
    FEATURE    = 0x02,
    RECALL     = 0x03,
    PRECALC    = 0x04,
    RANK_MASTER = 0x05,
    KVWORKER   = 0x06,
    REDIS      = 0x07,
    VLLM       = 0x08,
    DISCOVERY  = 0x09,
    RANK_SUB   = 0x0A,
    UNKNOWN    = 0xFF
};

/**
 * @brief 错误类型枚举
 */
enum class ErrorType : uint8_t {
    SUCCESS        = 0x00,
    INVALID_INPUT  = 0x01,
    RESOURCE_ERROR = 0x02,  // 内存、连接等资源错误
    SERVICE_ERROR  = 0x03,  // 依赖服务失败
    TIMEOUT        = 0x04,
    NOT_FOUND      = 0x05,
    UNAUTHORIZED   = 0x06,
    CONFIG_ERROR   = 0x07,
    NETWORK_ERROR  = 0x08,
    // 保留 0x09-0x0E
    INTERNAL       = 0x0F   // 系统内部错误
};

/**
 * @brief 构造错误码
 * @param module 模块代码
 * @param type 错误类型
 * @param specific_code 具体错误码 (0-65535)
 */
constexpr uint32_t MakeErrorCode(ModuleCode module, ErrorType type, uint16_t specific_code) {
    return (static_cast<uint32_t>(module) << 24) |
           (static_cast<uint32_t>(type) << 16) |
           static_cast<uint32_t>(specific_code);
}

/**
 * @brief 从错误码解析模块
 */
constexpr ModuleCode GetModuleFromCode(uint32_t code) {
    uint8_t module = static_cast<uint8_t>((code >> 24) & 0xFF);
    return static_cast<ModuleCode>(module);
}

/**
 * @brief 从错误码解析错误类型
 */
constexpr ErrorType GetTypeFromCode(uint32_t code) {
    uint8_t type = static_cast<uint8_t>((code >> 16) & 0xFF);
    return static_cast<ErrorType>(type);
}

/**
 * @brief 从错误码解析具体错误码
 */
constexpr uint16_t GetSpecificCodeFromCode(uint32_t code) {
    return static_cast<uint16_t>(code & 0xFFFF);
}

/**
 * @brief 检查错误码是否为成功
 */
constexpr bool IsSuccessCode(uint32_t code) {
    return GetTypeFromCode(code) == ErrorType::SUCCESS;
}

/**
 * @brief 获取模块名称字符串
 */
std::string ModuleToString(ModuleCode module);

/**
 * @brief 获取错误类型字符串
 */
std::string ErrorTypeToString(ErrorType type);

/**
 * @brief 错误码转换为字符串表示
 */
std::string ErrorCodeToString(uint32_t code);

// 常用成功错误码
constexpr uint32_t OK_CODE = MakeErrorCode(ModuleCode::COMMON, ErrorType::SUCCESS, 0);

// 通用错误码
namespace common_errors {
    constexpr uint32_t SUCCESS = OK_CODE;
    constexpr uint32_t UNKNOWN_ERROR = MakeErrorCode(ModuleCode::COMMON, ErrorType::INTERNAL, 0x0001);
    constexpr uint32_t INVALID_ARGUMENT = MakeErrorCode(ModuleCode::COMMON, ErrorType::INVALID_INPUT, 0x0002);
    constexpr uint32_t OUT_OF_MEMORY = MakeErrorCode(ModuleCode::COMMON, ErrorType::RESOURCE_ERROR, 0x0003);
    constexpr uint32_t FILE_NOT_FOUND = MakeErrorCode(ModuleCode::COMMON, ErrorType::NOT_FOUND, 0x0004);
    constexpr uint32_t PERMISSION_DENIED = MakeErrorCode(ModuleCode::COMMON, ErrorType::UNAUTHORIZED, 0x0005);
    constexpr uint32_t TIMEOUT_ERROR = MakeErrorCode(ModuleCode::COMMON, ErrorType::TIMEOUT, 0x0006);
    constexpr uint32_t NETWORK_ERROR = MakeErrorCode(ModuleCode::COMMON, ErrorType::NETWORK_ERROR, 0x0007);
    constexpr uint32_t CONFIG_ERROR = MakeErrorCode(ModuleCode::COMMON, ErrorType::CONFIG_ERROR, 0x0008);
}

namespace recall_errors {
    constexpr uint32_t VLLM_CHANNEL_INIT_FAILED  = MakeErrorCode(ModuleCode::RECALL, ErrorType::NETWORK_ERROR, 0x0001);
    constexpr uint32_t VLLM_REQUEST_FAILED       = MakeErrorCode(ModuleCode::RECALL, ErrorType::SERVICE_ERROR,  0x0002);
    constexpr uint32_t VLLM_RESPONSE_PARSE_FAILED = MakeErrorCode(ModuleCode::RECALL, ErrorType::SERVICE_ERROR, 0x0003);
    constexpr uint32_t VLLM_NO_CHOICES            = MakeErrorCode(ModuleCode::RECALL, ErrorType::SERVICE_ERROR, 0x0004);
    constexpr uint32_t VLLM_NO_CONTENT            = MakeErrorCode(ModuleCode::RECALL, ErrorType::SERVICE_ERROR, 0x0005);
    constexpr uint32_t SKU_PARSE_FAILED           = MakeErrorCode(ModuleCode::RECALL, ErrorType::SERVICE_ERROR, 0x0006);
    constexpr uint32_t NO_SKU_RETURNED            = MakeErrorCode(ModuleCode::RECALL, ErrorType::NOT_FOUND,     0x0007);
    constexpr uint32_t EMPTY_USER_ID              = MakeErrorCode(ModuleCode::RECALL, ErrorType::INVALID_INPUT,  0x0008);
    constexpr uint32_t INTERNAL_ERROR             = MakeErrorCode(ModuleCode::RECALL, ErrorType::INTERNAL,      0x0009);
}

namespace precalc_errors {
    constexpr uint32_t EMPTY_USER_FEAT           = MakeErrorCode(ModuleCode::PRECALC, ErrorType::INVALID_INPUT,  0x0001);
    constexpr uint32_t KVCLIENT_INIT_FAILED      = MakeErrorCode(ModuleCode::PRECALC, ErrorType::SERVICE_ERROR,  0x0002);
    constexpr uint32_t KVCLIENT_CREATE_FAILED    = MakeErrorCode(ModuleCode::PRECALC, ErrorType::SERVICE_ERROR,  0x0003);
    constexpr uint32_t KVCLIENT_SET_FAILED       = MakeErrorCode(ModuleCode::PRECALC, ErrorType::SERVICE_ERROR,  0x0004);
    constexpr uint32_t INTERNAL_ERROR            = MakeErrorCode(ModuleCode::PRECALC, ErrorType::INTERNAL,      0x0005);
}

namespace rank_master_errors {
    constexpr uint32_t EMPTY_USER_FEAT_KEY        = MakeErrorCode(ModuleCode::RANK_MASTER, ErrorType::INVALID_INPUT,  0x0001);
    constexpr uint32_t EMPTY_SKUS                 = MakeErrorCode(ModuleCode::RANK_MASTER, ErrorType::INVALID_INPUT,  0x0002);
    constexpr uint32_t NO_SKU_PARSED              = MakeErrorCode(ModuleCode::RANK_MASTER, ErrorType::NOT_FOUND,     0x0003);
    constexpr uint32_t SUB_WORKER_CALL_FAILED     = MakeErrorCode(ModuleCode::RANK_MASTER, ErrorType::SERVICE_ERROR,  0x0004);
    constexpr uint32_t SUB_WORKER_CHANNEL_INVALID = MakeErrorCode(ModuleCode::RANK_MASTER, ErrorType::RESOURCE_ERROR, 0x0005);
    constexpr uint32_t INTERNAL_ERROR             = MakeErrorCode(ModuleCode::RANK_MASTER, ErrorType::INTERNAL,      0x0006);
}

namespace feature_errors {
    constexpr uint32_t EMPTY_USER_ID        = MakeErrorCode(ModuleCode::FEATURE, ErrorType::INVALID_INPUT,  0x0001);
    constexpr uint32_t EMPTY_SKU_IDS        = MakeErrorCode(ModuleCode::FEATURE, ErrorType::INVALID_INPUT,  0x0002);
    constexpr uint32_t INTERNAL_ERROR       = MakeErrorCode(ModuleCode::FEATURE, ErrorType::INTERNAL,      0x0003);
}

namespace rank_sub_errors {
    constexpr uint32_t EMPTY_USER_FEAT_KEY   = MakeErrorCode(ModuleCode::RANK_SUB, ErrorType::INVALID_INPUT,  0x0001);
    constexpr uint32_t EMPTY_SKUS_SUB        = MakeErrorCode(ModuleCode::RANK_SUB, ErrorType::INVALID_INPUT,  0x0002);
    constexpr uint32_t KVCLIENT_INIT_FAILED  = MakeErrorCode(ModuleCode::RANK_SUB, ErrorType::SERVICE_ERROR,  0x0003);
    constexpr uint32_t KVCLIENT_GET_FAILED   = MakeErrorCode(ModuleCode::RANK_SUB, ErrorType::SERVICE_ERROR,  0x0004);
    constexpr uint32_t NO_SKU_PARSED         = MakeErrorCode(ModuleCode::RANK_SUB, ErrorType::NOT_FOUND,     0x0005);
    constexpr uint32_t INTERNAL_ERROR        = MakeErrorCode(ModuleCode::RANK_SUB, ErrorType::INTERNAL,      0x0006);
}

} // namespace error
} // namespace common

#endif // COMMON_ERROR_ERROR_CODE_H