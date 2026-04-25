# 错误码体系设计方案

## 1. 设计目标

### 1.1 核心目标
- **统一错误处理**：为整个微服务架构提供一致的错误表示和处理机制
- **结构化错误信息**：包含模块、错误类型、具体错误码三层信息
- **可观测性**：支持错误统计、监控告警和问题诊断
- **跨服务传播**：支持错误在微服务间传递和聚合

### 1.2 设计原则
- **强类型安全**：使用枚举而非魔法数字，编译时检查
- **高效传输**：32位整型编码，适合RPC传输
- **可读可查**：支持错误码到字符串的转换，便于日志和调试
- **渐进采用**：兼容现有错误处理模式，平滑迁移

## 2. 系统架构

### 2.1 错误码结构
```
0x MM TT CC CC
│   │  └───── 具体错误码 (16 bits, 0-65535)
│   └─────── 错误类型 (8 bits, 0-15)
└─────────── 模块代码 (8 bits, 0-15)
```

### 2.2 核心组件
```
error_code.h/cpp  # 错误码定义和编解码
status.h/cpp      # 错误状态封装类
error.h          # 公共接口和快捷函数
```

## 3. 详细设计

### 3.1 模块代码定义
```cpp
enum class ModuleCode : uint8_t {
    COMMON     = 0x00,  // 通用模块
    GATEWAY    = 0x01,  // 网关服务
    FEATURE    = 0x02,  // 特征服务
    RECALL     = 0x03,  // 召回服务
    PRECALC    = 0x04,  // 前置计算服务
    RANK       = 0x05,  // 精排服务
    KVWORKER   = 0x06,  // KVWorker服务
    REDIS      = 0x07,  // Redis服务
    VLLM       = 0x08,  // vLLM服务
    UNKNOWN    = 0xFF   // 未知模块
};
```

### 3.2 错误类型定义
```cpp
enum class ErrorType : uint8_t {
    SUCCESS        = 0x00,  // 成功
    INVALID_INPUT  = 0x01,  // 输入无效
    RESOURCE_ERROR = 0x02,  // 资源错误（内存、连接等）
    SERVICE_ERROR  = 0x03,  // 依赖服务错误
    TIMEOUT        = 0x04,  // 超时错误
    NOT_FOUND      = 0x05,  // 资源未找到
    UNAUTHORIZED   = 0x06,  // 权限错误
    CONFIG_ERROR   = 0x07,  // 配置错误
    NETWORK_ERROR  = 0x08,  // 网络错误
    INTERNAL       = 0x0F   // 系统内部错误
};
```

### 3.3 错误码编解码
```cpp
// 构造错误码
constexpr uint32_t MakeErrorCode(ModuleCode module, 
                                 ErrorType type, 
                                 uint16_t specific_code);

// 解析错误码
constexpr ModuleCode GetModuleFromCode(uint32_t code);
constexpr ErrorType GetTypeFromCode(uint32_t code);
constexpr uint16_t GetSpecificCodeFromCode(uint32_t code);

// 常用错误码定义
namespace common_errors {
    constexpr uint32_t SUCCESS = OK_CODE;
    constexpr uint32_t INVALID_ARGUMENT = MakeErrorCode(ModuleCode::COMMON, 
                                                       ErrorType::INVALID_INPUT, 0x0002);
    constexpr uint32_t OUT_OF_MEMORY = MakeErrorCode(ModuleCode::COMMON,
                                                    ErrorType::RESOURCE_ERROR, 0x0003);
    // ... 其他错误码
}
```

### 3.4 Status类设计
```cpp
class Status {
public:
    // 构造和检查
    Status();  // 默认成功
    Status(uint32_t code, std::string message = "");
    static Status OK();
    static Status Error(uint32_t code, std::string message = "");
    
    // 状态检查
    bool IsOk() const;
    bool IsError() const;
    operator bool() const;  // 布尔转换，用于条件检查
    
    // 错误信息获取
    uint32_t Code() const;
    const std::string& Message() const;
    ModuleCode GetModule() const;
    ErrorType GetType() const;
    uint16_t GetSpecificCode() const;
    
    // 字符串表示
    std::string ToString() const;
    
private:
    uint32_t code_;
    std::string message_;
};
```

## 4. 使用场景

### 4.1 服务端返回错误
```cpp
// 在recall服务中定义具体错误码
namespace recall::errors {
    constexpr uint32_t VLLM_CONNECTION_FAILED = 
        MakeErrorCode(ModuleCode::RECALL, ErrorType::SERVICE_ERROR, 0x0001);
    constexpr uint32_t JSON_PARSE_ERROR = 
        MakeErrorCode(ModuleCode::RECALL, ErrorType::INVALID_INPUT, 0x0002);
}

// 服务实现中返回错误
Status RecallServiceImpl::Recall(RecallRequest* request,
                                 RecallResponse* response) {
    if (request->user_logs_size() == 0) {
        return Status::Error(recall::errors::INVALID_INPUT,
                            "user_logs cannot be empty");
    }
    
    if (!ConnectToVLLM()) {
        return Status::Error(recall::errors::VLLM_CONNECTION_FAILED,
                            "Failed to connect to vLLM service");
    }
    
    // 成功返回
    return Status::OK();
}
```

### 4.2 客户端错误处理
```cpp
// 客户端调用服务
Status status = stub->Recall(&request, &response, nullptr);
if (!status) {
    // 日志记录错误
    LOG_ERROR("Recall failed: {}", status.ToString());
    
    // 根据错误类型采取不同处理策略
    switch (status.GetType()) {
        case ErrorType::INVALID_INPUT:
            // 输入错误，提示用户
            ShowUserError("Invalid input: " + status.Message());
            break;
        case ErrorType::SERVICE_ERROR:
            // 服务错误，尝试重试
            if (CanRetry()) {
                RetryWithBackoff();
            }
            break;
        case ErrorType::TIMEOUT:
            // 超时错误，调整超时设置
            AdjustTimeoutSettings();
            break;
        default:
            // 其他错误
            HandleGenericError(status);
    }
}
```

### 4.3 错误聚合和传播
```cpp
// 网关服务聚合多个服务的错误
Status Gateway::ProcessRequest(const UserRequest& req) {
    Status feature_status = feature_client_->GetUserFeatures(req);
    if (!feature_status) {
        return Status::Error(
            MakeErrorCode(ModuleCode::GATEWAY, ErrorType::SERVICE_ERROR, 0x0001),
            "Feature service failed: " + feature_status.Message()
        );
    }
    
    Status recall_status = recall_client_->Recall(req);
    Status rank_status = rank_client_->Rank(req);
    
    // 如果有多个错误，聚合主要错误
    if (!recall_status || !rank_status) {
        return Status::Error(
            MakeErrorCode(ModuleCode::GATEWAY, ErrorType::SERVICE_ERROR, 0x0002),
            "Multiple services failed"
        );
    }
    
    return Status::OK();
}
```

## 5. 扩展机制

### 5.1 自定义错误码注册
```cpp
// 预留错误码注册接口
class ErrorRegistry {
public:
    static void Register(uint32_t code,
                         std::string_view module_name,
                         std::string_view error_name,
                         std::string_view description,
                         std::string_view recovery_suggestion = "");
    
    static ErrorInfo Lookup(uint32_t code);
};

// 使用示例
ErrorRegistry::Register(
    recall::errors::VLLM_CONNECTION_FAILED,
    "RECALL",
    "VLLM_CONNECTION_FAILED",
    "Failed to connect to vLLM service",
    "Check vLLM service status and network connectivity"
);
```

### 5.2 错误码映射到HTTP状态码
```cpp
// 错误类型到HTTP状态码的映射
std::optional<int> ErrorTypeToHttpStatusCode(ErrorType type) {
    switch (type) {
        case ErrorType::INVALID_INPUT:    return 400; // Bad Request
        case ErrorType::UNAUTHORIZED:     return 401; // Unauthorized
        case ErrorType::NOT_FOUND:        return 404; // Not Found
        case ErrorType::SERVICE_ERROR:    return 503; // Service Unavailable
        case ErrorType::TIMEOUT:          return 504; // Gateway Timeout
        default:                          return 500; // Internal Server Error
    }
}
```

## 6. 监控和告警集成

### 6.1 错误统计
```cpp
class ErrorMetrics {
public:
    void RecordError(const Status& status) {
        auto module = status.GetModule();
        auto type = status.GetType();
        
        // 统计各模块错误
        module_errors_[module]++;
        type_errors_[type]++;
        
        // 具体错误码统计
        specific_errors_[status.Code()]++;
        
        // 如果是重要错误，触发告警
        if (IsCriticalError(status)) {
            SendAlert(status);
        }
    }
    
private:
    std::unordered_map<ModuleCode, uint64_t> module_errors_;
    std::unordered_map<ErrorType, uint64_t> type_errors_;
    std::unordered_map<uint32_t, uint64_t> specific_errors_;
};
```

### 6.2 错误率告警规则
```
# 告警规则示例
- 模块错误率 > 5% (5分钟内)
- 特定错误码连续出现 > 10次 (1分钟内)
- 系统级错误(INTERNAL)立即告警
```

## 7. 最佳实践

### 7.1 错误码定义规范
1. **模块分配**：每个服务分配唯一模块代码
2. **错误分类**：按错误类型合理分类
3. **具体错误码**：从0x0001开始顺序分配，预留范围
4. **文档化**：每个错误码应有清晰的文档说明

### 7.2 错误处理最佳实践
1. **尽早失败**：在输入验证阶段就返回错误
2. **提供上下文**：错误消息应包含足够调试信息
3. **避免嵌套**：不要过度包装错误，保持调用链清晰
4. **日志记录**：重要错误应记录到日志系统

### 7.3 客户端处理建议
1. **分类处理**：根据错误类型采取不同策略
2. **重试机制**：对临时性错误实现指数退避重试
3. **降级策略**：对非关键错误实现服务降级
4. **用户提示**：对外暴露友好的错误信息

## 8. 演进路线

### 8.1 短期目标 (V1.0)
- ✅ 完成基础错误码体系实现
- ✅ 集成到现有服务中
- ✅ 提供基本错误统计

### 8.2 中期目标 (V1.1)
- 🔄 错误码注册表和管理工具
- 🔄 错误码到多语言的映射
- 🔄 与现有监控系统集成

### 8.3 长期目标 (V2.0)
- 📋 分布式错误追踪
- 📋 自动错误分析和根因定位
- 📋 智能错误恢复建议

## 9. 附录

### 9.1 错误码分配表
| 模块 | 代码 | 负责人 | 备注 |
|------|------|--------|------|
| COMMON | 0x00 | 基础团队 | 通用错误 |
| GATEWAY | 0x01 | 网关团队 | 网关服务 |
| FEATURE | 0x02 | 特征团队 | 特征服务 |
| RECALL | 0x03 | 召回团队 | 召回服务 |
| PRECALC | 0x04 | 计算团队 | 前置计算 |
| RANK | 0x05 | 排序团队 | 精排服务 |

### 9.2 常见错误码示例
| 错误码 (十六进制) | 模块 | 类型 | 具体码 | 描述 |
|-------------------|------|------|--------|------|
| 0x00000000 | COMMON | SUCCESS | 0x0000 | 成功 |
| 0x00010002 | COMMON | INVALID_INPUT | 0x0002 | 无效参数 |
| 0x03030001 | RECALL | SERVICE_ERROR | 0x0001 | vLLM连接失败 |
| 0x03010002 | RECALL | INVALID_INPUT | 0x0002 | JSON解析错误 |

---

**设计者**：系统架构组  
**版本**：1.0  
**最后更新**：2025-04-17  
**相关文档**：[日志系统设计方案](./logger_system_design.md)