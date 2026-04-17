# PrecalcService 和 RecallService 修复计划

## 问题概述

用户提出了 3 个需要修复的问题：

1. **recall_server.cpp**: SKU ID 生成方式不合理，应该在 prompt 中告诉大模型需要生成多少个 SKU ID，而不是在代码中补充
2. **precalc_server.cpp**: 写入 KVWorker 应该直接使用元戎接口，而不是 BRPC 占位实现
3. **precalc_server.cpp**: payload 应该是随机生成的乱码，payload 大小 + 元戎返回的 key 大小一共 100KB

## 解决方案

### 问题 1: recall_server.cpp - 修改 Prompt 让大模型生成指定数量的 SKU ID

**修改位置**: `services/recall/server/recall_server.cpp`

**修改内容**:
1. 在 `build_vllm_request` 函数的 system message 中，明确告诉大模型需要生成的 SKU ID 数量
2. 移除 `parse_vllm_response` 函数中的 SKU 数量补充逻辑
3. 保留 SKU 数量不足的日志记录，但不强制补充

**修改后的 system message 示例**:
```cpp
system_msg.AddMember("content", 
    "你是一个搜推广助手，请根据用户特征和日志返回推荐的 SKU ID 列表。\n"
    "请恰好生成 " + std::to_string(max_sku_count) + " 个 SKU ID，不要多也不要少。\n"
    "返回格式：用逗号分隔的数字，例如：12345,67890,11111,...", 
    allocator);
```

### 问题 2: precalc_server.cpp - 使用元戎接口写入 KVWorker

**元戎接口完整信息**（从官方文档）:

**头文件**:
```cpp
#include <datasystem/kv_client.h>
```

**命名空间**: `datasystem`

**完整使用示例**:
```cpp
#include <datasystem/kv_client.h>
using namespace datasystem;

// 1. 配置连接选项
ConnectOptions connectOptions = { .host = "127.0.0.1", .port = 31501 };

// 2. 创建 KV 客户端
KVClient kv_client(connectOptions);

// 3. 初始化连接
Status status = kv_client.Init();
if (!status.IsOk()) {
    LOG(ERROR) << "KVClient init failed: " << status.ToString();
    return;
}

// 4. 准备要写入的数据
std::string payload = generate_payload();  // 生成的 payload 数据

// 5. 配置 Set 参数（包括 TTL）
SetParam param;
param.ttlSecond = 5;  // 设置 TTL 为 5 秒
param.writeMode = WriteMode::NONE_L2_CACHE;  // 不写入二级缓存
param.existence = ExistenceOpt::NONE;  // 不做存在性检查
param.cacheType = CacheType::MEMORY;  // 使用内存缓存介质

// 6. 写入数据（使用返回键的接口）
std::string generated_key = kv_client.Set(payload, param);
if (generated_key.empty()) {
    LOG(ERROR) << "KVClient Set failed";
    return;
}

// 7. 将生成的 key 返回给客户端
response->set_user_feat_key(generated_key);
```

**关键接口说明**:

1. **`std::string Set(const StringView &val, const SetParam &param)`**
   - 设置键值对数据缓存到数据系统，并返回生成的键
   - 参数：
     - `val`: 需要缓存的值
     - `param`: 设置参数，包括 TTL 等
   - 返回：生成的键，如果设置失败则返回空字符串

2. **`SetParam` 结构体**:
   - `uint32_t ttlSecond`: 设置 Key 的存活时间，单位：秒（默认 0 表示不设置）
   - `WriteMode writeMode`: 数据可靠性级别（默认 `NONE_L2_CACHE`）
   - `ExistenceOpt existence`: 键存在时的行为（默认 `NONE`）
   - `CacheType cacheType`: 缓存介质（默认 `MEMORY`）

**修改内容**:
1. 添加元戎头文件 `#include <datasystem/kv_client.h>`
2. 移除 `write_to_kvworker` 函数和 BRPC Channel 相关代码
3. 使用元戎的 `Set` 接口（返回键的版本）
4. 在 `SetParam` 中设置 `ttlSecond = FLAGS_ttl_seconds`
5. 直接使用元戎返回的 key，不再需要自己构造 user_id+ 时间戳

### 问题 3: precalc_server.cpp - 修改 payload 生成逻辑

**修改位置**: `services/PrecalcService/server/precalc_server.cpp`

**修改内容**:
1. 添加新的配置参数 `total_payload_size_kb`（默认 100KB）
2. 修改 payload 生成逻辑：
   - 先生成 payload 数据
   - 使用元戎 Set 接口写入，获取返回的 key
   - 计算 key 的实际大小
   - 调整 payload 大小，确保 `payload.size() + key.size() == total_payload_size_kb * 1024`
3. 更新客户端的期望大小参数

**修改后的代码示例**:
```cpp
DEFINE_int32(total_payload_size_kb, 100, "payload 和 key 总大小（KB），默认 100KB");

// 生成初始 payload（预留一些空间给 key）
std::string generate_initial_payload(size_t size_kb) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    size_t total_bytes = size_kb * 1024;
    std::string payload;
    payload.resize(total_bytes);
    
    for (size_t i = 0; i < total_bytes; ++i) {
        payload[i] = static_cast<char>(dis(gen));
    }
    
    return payload;
}

// 调整 payload 大小，确保 payload + key = total_payload_size_kb
std::string adjust_payload_size(std::string payload, const std::string& key, size_t total_size_kb) {
    size_t total_bytes = total_size_kb * 1024;
    size_t key_size = key.size();
    size_t expected_payload_size = total_bytes - key_size;
    
    if (payload.size() > expected_payload_size) {
        payload.resize(expected_payload_size);
    }
    
    LOG(INFO) << "Adjusted payload size: " << payload.size() 
              << " bytes, key size: " << key.size() 
              << " bytes, total: " << (payload.size() + key.size()) << " bytes";
    
    return payload;
}

// 使用流程
std::string payload = generate_initial_payload(FLAGS_total_payload_size_kb);
SetParam param;
param.ttlSecond = FLAGS_ttl_seconds;
std::string key = kv_client.Set(payload, param);

// 调整 payload 大小
payload = adjust_payload_size(payload, key, FLAGS_total_payload_size_kb);

// 重新写入调整后的 payload（或者直接使用初始写入的 key）
// 注意：这里可能需要重新 Set 一次，或者接受第一次的 key
```

**注意**: 由于元戎的 Set 接口会返回生成的 key，我们需要：
- 方案 A: 先写入一个较大的 payload，获取 key 后调整 payload 大小，然后重新写入
- 方案 B: 先预估 key 的大小（比如 100 字节），生成 payload 时预留这个空间

**建议采用方案 B**，更简单高效：
```cpp
// 预估 key 的大小（元戎生成的 key 通常包含 Worker ID 和前缀，约 100 字节）
constexpr size_t ESTIMATED_KEY_SIZE = 100;

size_t payload_size = FLAGS_total_payload_size_kb * 1024 - ESTIMATED_KEY_SIZE;
std::string payload = generate_payload(payload_size);

SetParam param;
param.ttlSecond = FLAGS_ttl_seconds;
std::string key = kv_client.Set(payload, param);

// 验证总大小
LOG(INFO) << "Total size: " << (key.size() + payload.size()) << " bytes";
```

### 步骤 3: 修改 precalc_test_client.cpp
- 更新期望 payload 大小的参数（改为 100KB）
- 更新输出信息，显示 payload + key 的总大小

### 步骤 4: 创建端口文档
- 创建 `docs/ports.md` 文件
- 记录所有服务的端口配置

### 步骤 5: 验证和测试
- 检查编译是否通过
- 验证功能是否正常

## 实施步骤

### 步骤 1: 修改 recall_server.cpp
- 修改 `build_vllm_request` 函数，在 system message 中添加 SKU 数量要求
- 修改 `parse_vllm_response` 函数，移除 SKU 补充逻辑
- 更新相关日志

### 步骤 2: 修改 precalc_server.cpp
- 添加元戎头文件 `#include <datasystem/kv_client.h>`
- 添加命名空间 `using namespace datasystem;`
- 移除 `write_to_kvworker` 函数
- 移除 KVWorker Channel 相关代码（`kvworker_channel_` 成员变量和 `init_kvworker_channel` 函数）
- 在 `Precalculate` 方法中直接使用元戎 KVClient
- 修改 `result_size_mb` 为 `total_payload_size_kb`（默认 100KB）
- 修改 payload 生成逻辑（考虑 key 的大小）
- 使用元戎返回的 key，移除 user_feat_key 构造逻辑

### 步骤 3: 修改 precalc_test_client.cpp
- 更新期望 payload 大小的参数（改为 100KB）
- 更新输出信息

### 步骤 4: 创建端口文档
- 创建 `docs/ports.md`
- 记录以下端口：
  - Proxy: 8000
  - RecallService: 8001
  - FeatureService: 8002
  - PrecalcService: 8004
  - RankService: 8005
  - KVWorker: 31501

### 步骤 5: 验证和测试
- 检查编译是否通过
- 验证功能是否正常

## 注意事项

1. **元戎接口细节**:
   - 使用 `KVClient` 而不是 `DsClient`
   - 使用 `Set(val, param)` 接口，它会自动生成 key 并返回
   - TTL 通过 `SetParam.ttlSecond` 设置

2. **错误处理**:
   - `Init()` 返回 `Status` 对象，需要检查 `IsOk()`
   - `Set()` 返回 `std::string`，如果失败返回空字符串

3. **默认端口**:
   - 元戎 KVWorker 默认端口：31501

4. **payload 大小计算**:
   - 总大小 = payload 大小 + key 大小
   - 需要预估 key 的大小（约 100 字节）
   - 或者先写入再调整
