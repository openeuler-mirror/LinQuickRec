#include "common/internal/error/error_code.h"
#include <unordered_map>
#include <string>

namespace common {
namespace error {

std::string ModuleToString(ModuleCode module) {
    static const std::unordered_map<ModuleCode, std::string> module_map = {
        {ModuleCode::COMMON,    "COMMON"},
        {ModuleCode::GATEWAY,   "GATEWAY"},
        {ModuleCode::FEATURE,   "FEATURE"},
        {ModuleCode::RECALL,    "RECALL"},
        {ModuleCode::PRECALC,   "PRECALC"},
        {ModuleCode::RANK_MASTER, "RANK_MASTER"},
        {ModuleCode::KVWORKER,  "KVWORKER"},
        {ModuleCode::REDIS,     "REDIS"},
        {ModuleCode::VLLM,      "VLLM"},
        {ModuleCode::DISCOVERY, "DISCOVERY"},
        {ModuleCode::RANK_SUB,  "RANK_SUB"},
        {ModuleCode::UNKNOWN,   "UNKNOWN"}
    };
    
    auto it = module_map.find(module);
    if (it != module_map.end()) {
        return it->second;
    }
    return "UNKNOWN_MODULE";
}

std::string ErrorTypeToString(ErrorType type) {
    static const std::unordered_map<ErrorType, std::string> type_map = {
        {ErrorType::SUCCESS,        "SUCCESS"},
        {ErrorType::INVALID_INPUT,  "INVALID_INPUT"},
        {ErrorType::RESOURCE_ERROR, "RESOURCE_ERROR"},
        {ErrorType::SERVICE_ERROR,  "SERVICE_ERROR"},
        {ErrorType::TIMEOUT,        "TIMEOUT"},
        {ErrorType::NOT_FOUND,      "NOT_FOUND"},
        {ErrorType::UNAUTHORIZED,   "UNAUTHORIZED"},
        {ErrorType::CONFIG_ERROR,   "CONFIG_ERROR"},
        {ErrorType::NETWORK_ERROR,  "NETWORK_ERROR"},
        {ErrorType::INTERNAL,       "INTERNAL"}
    };
    
    auto it = type_map.find(type);
    if (it != type_map.end()) {
        return it->second;
    }
    return "UNKNOWN_TYPE";
}

std::string ErrorCodeToString(uint32_t code) {
    if (IsSuccessCode(code)) {
        return "SUCCESS";
    }
    
    ModuleCode module = GetModuleFromCode(code);
    ErrorType type = GetTypeFromCode(code);
    uint16_t specific = GetSpecificCodeFromCode(code);
    
    return ModuleToString(module) + "::" + 
           ErrorTypeToString(type) + "[" + 
           std::to_string(specific) + "]";
}

} // namespace error
} // namespace common