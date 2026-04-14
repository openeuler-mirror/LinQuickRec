# Recall 服务优化方案

## 📋 问题分析

### 当前代码存在的问题

通过仔细检查 `brpc_server.cpp` 和 `brpc_client.cpp`，发现以下需要优化的问题：

#### 1. **JSON 处理** ❌
- **问题**：使用手写辅助函数 `escape_json_string()`、`extract_json_string()`、`extract_json_int()`
- **风险**：
  - 代码注释自己标注"极其简陋"、"生产环境请用专业库"
  - 不支持嵌套 JSON 结构解析
  - 转义处理不完整，容易出错
  - 性能差，无错误处理机制

#### 2. **延迟计算** ⚠️
- **问题**：仅计算端到端总延迟，缺少关键时间节点记录
- **现状**：
  ```cpp
  int64_t start_us = butil::gettimeofday_us();
  // ... 所有处理逻辑 ...
  int64_t end_us = butil::gettimeofday_us();
  response->set_latency_ms((end_us - start_us)/1000);
  ```
- **不足**：
  - 无法区分各阶段耗时（验证、HTTP 请求、解析等）
  - 不支持 trace_id 全链路追踪
  - 网关无法统一采集和分析

#### 3. **缺少请求验证** ❌
- **问题**：完全没有参数验证
- **风险**：
  - 可能接受空 prompt、超大 prompt
  - temperature、top_p 等参数无范围检查
  - max_tokens 无限制
  - 存在安全隐患

#### 4. **并发处理能力** ⚠️
- **问题**：单线程同步阻塞处理
- **影响**：
  - 每个请求占用一个 BRPC 工作线程
  - 调用 vLLM 时线程阻塞等待
  - 并发能力受限

#### 5. **错误处理** ⚠️
- **问题**：错误处理不完善
- **表现**：
  - 没有区分客户端错误和服务端错误
  - 缺少重试机制
  - 缺少降级策略

#### 6. **客户端优化** ⚠️
- **问题**：
  - 缺少超时控制配置
  - 缺少重试逻辑
  - 缺少连接池管理

---

## 🎯 优化方案

### 方案总览

| 优化项 | 优化方案 | 使用库/技术 | 优先级 |
|--------|----------|-------------|--------|
| JSON 处理 | 使用 RapidJSON | RapidJSON | P0 |
| 延迟计算 | 基于 trace_id 的关键节点记录 | 轻量级探针 | P0 |
| 请求验证 | 添加参数验证逻辑 | 原生 C++ | P0 |
| 并发处理 | 线程池 | 原生 C++11 | P1 |
| 错误处理 | 完善错误码和降级 | 原生 C++ | P1 |
| 客户端 | 连接池 + 重试 | BRPC 内置 | P2 |

---

## 📦 详细优化方案

### 1. JSON 处理 - 使用 RapidJSON ⭐⭐⭐⭐⭐

#### 优化方案
**使用 RapidJSON 专业库替代手写函数**

#### 依赖库
- **RapidJSON**：腾讯开源的高性能 C++ JSON 解析库
- 特点：
  - 比标准库快 10 倍以上
  - 内存友好，零拷贝解析
  - 完整的 JSON Schema 支持
  - 经过生产环境验证

#### 安装方式
```bash
# Ubuntu/Debian
apt-get install rapidjson-dev

# 源码安装
git clone https://github.com/Tencent/rapidjson.git
cd rapidjson && mkdir build && cd build
cmake .. && make && make install
```

#### 代码示例

**构造 JSON 请求**：
```cpp
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace rapidjson;

// 构造 vLLM 请求
Document d;
d.SetObject();
Document::AllocatorType& allocator = d.GetAllocator();

d.AddMember("model", Value(MODEL_NAME, allocator).Move(), allocator);

// 构造 messages 数组
Value messages(kArrayType);
Value message(kObjectType);
message.AddMember("role", "user", allocator);
message.AddMember("content", Value(request->prompt().c_str(), allocator).Move(), allocator);
messages.PushBack(message, allocator);
d.AddMember("messages", messages, allocator);

d.AddMember("max_tokens", request->max_tokens(), allocator);
d.AddMember("temperature", request->temperature(), allocator);
d.AddMember("top_p", request->top_p(), allocator);
d.AddMember("stream", false, allocator);

// 序列化为字符串
StringBuffer buffer;
Writer<StringBuffer> writer(buffer);
d.Accept(writer);
std::string json_body = buffer.GetString();
```

**解析 JSON 响应**：
```cpp
#include <rapidjson/document.h>

using namespace rapidjson;

Document d;
d.Parse(resp_body.c_str());

if (d.HasParseError()) {
    LOG(ERROR) << "JSON parse error: " << d.GetParseError();
    return false;
}

// 提取 content
std::string content;
if (d.HasMember("choices") && d["choices"].IsArray() 
    && d["choices"].Size() > 0
    && d["choices"][0].HasMember("message")
    && d["choices"][0]["message"].HasMember("content")) {
    content = d["choices"][0]["message"]["content"].GetString();
}

// 提取 usage
int total_tokens = 0, prompt_tokens = 0, completion_tokens = 0;
if (d.HasMember("usage")) {
    const Value& usage = d["usage"];
    if (usage.HasMember("total_tokens")) 
        total_tokens = usage["total_tokens"].GetInt();
    if (usage.HasMember("prompt_tokens")) 
        prompt_tokens = usage["prompt_tokens"].GetInt();
    if (usage.HasMember("completion_tokens")) 
        completion_tokens = usage["completion_tokens"].GetInt();
}
```

#### 优势
- ✅ 专业库，经过生产验证
- ✅ 支持复杂 JSON 结构
- ✅ 性能优异
- ✅ 完整的错误处理
- ✅ 代码更简洁可维护

---

### 2. 延迟计算 - 基于 trace_id 的关键节点记录 ⭐⭐⭐⭐⭐

#### 优化方案
**配合轻量级探针，记录关键时间节点**

#### 设计思路
1. 网关服务生成唯一 `trace_id` 并在请求中传递
2. Recall 服务在关键节点记录时间戳
3. 网关统一采集和分析所有服务的时延数据

#### 关键时间节点

```cpp
struct RecallTimingPoints {
    // 时间戳（微秒）
    int64_t request_received_us = 0;      // 请求到达时间
    int64_t validation_start_us = 0;       // 验证开始时间
    int64_t validation_end_us = 0;         // 验证结束时间
    int64_t vllm_request_start_us = 0;     // vLLM 请求开始时间
    int64_t vllm_request_end_us = 0;       // vLLM 请求结束时间
    int64_t response_parse_start_us = 0;   // 响应解析开始时间
    int64_t response_parse_end_us = 0;     // 响应解析结束时间
    int64_t response_sent_us = 0;          // 响应返回时间
    
    // 计算各阶段耗时（毫秒）
    int64_t validation_duration_ms() const {
        return (validation_end_us - validation_start_us) / 1000;
    }
    
    int64_t vllm_duration_ms() const {
        return (vllm_request_end_us - vllm_request_start_us) / 1000;
    }
    
    int64_t parse_duration_ms() const {
        return (response_parse_end_us - response_parse_start_us) / 1000;
    }
    
    int64_t total_duration_ms() const {
        return (response_sent_us - request_received_us) / 1000;
    }
};
```

#### 代码示例

```cpp
#include "recommend.pb.h"
#include <brpc/controller.h>
#include <butil/time.h>

// trace_id 从请求中获取
std::string trace_id = request->trace_id();

// 记录时间节点
RecallTimingPoints timing;
timing.request_received_us = butil::gettimeofday_us();

// 1. 验证阶段
timing.validation_start_us = butil::gettimeofday_us();
auto validation_result = validate_request(request);
timing.validation_end_us = butil::gettimeofday_us();

if (validation_result != ValidationResult::VALID) {
    // 记录验证失败
    LOG(INFO) << "trace_id=" << trace_id 
              << ", validation_failed"
              << ", duration_ms=" << timing.validation_duration_ms();
    return;
}

// 2. vLLM 调用阶段
timing.vllm_request_start_us = butil::gettimeofday_us();
auto vllm_result = call_vllm(request);
timing.vllm_request_end_us = butil::gettimeofday_us();

// 3. 解析阶段
timing.response_parse_start_us = butil::gettimeofday_us();
parse_vllm_response(vllm_result, response);
timing.response_parse_end_us = butil::gettimeofday_us();

// 4. 返回响应
timing.response_sent_us = butil::gettimeofday_us();

// 记录完整时延信息（供探针采集）
LOG(INFO) << "trace_id=" << trace_id
          << ", total_ms=" << timing.total_duration_ms()
          << ", validation_ms=" << timing.validation_duration_ms()
          << ", vllm_ms=" << timing.vllm_duration_ms()
          << ", parse_ms=" << timing.parse_duration_ms();

// 可选：将 timing 信息添加到 response 的扩展字段中
// 或者通过探针接口上报
```

#### 与探针集成

**方案 A：通过日志采集**
```cpp
// 使用结构化日志格式，便于探针解析
LOG(INFO) << "{\"trace_id\":\"" << trace_id << "\","
          << "\"service\":\"recall\","
          << "\"timings\":{"
          << "\"total_ms\":" << timing.total_duration_ms() << ","
          << "\"validation_ms\":" << timing.validation_duration_ms() << ","
          << "\"vllm_ms\":" << timing.vllm_duration_ms() << ","
          << "\"parse_ms\":" << timing.parse_duration_ms()
          << "}}";
```

**方案 B：通过探针接口**
```cpp
// 假设有统一的探针接口
extern void record_timing_point(const std::string& trace_id,
                               const std::string& service_name,
                               const RecallTimingPoints& timing);

// 在关键节点调用
record_timing_point(trace_id, "recall", timing);
```

#### 优势
- ✅ 配合全链路追踪
- ✅ 细粒度时延分析
- ✅ 便于定位性能瓶颈
- ✅ 网关统一分析
- ✅ 轻量级，不影响主流程

---

### 3. 请求验证 - 参数校验 ⭐⭐⭐⭐⭐

#### 优化方案
**添加完善的参数验证逻辑**

#### 验证规则

```cpp
class RequestValidator {
public:
    enum class ValidationResult {
        VALID = 0,
        INVALID_PROMPT,
        PROMPT_TOO_LONG,
        INVALID_MAX_TOKENS,
        INVALID_TEMPERATURE,
        INVALID_TOP_P,
        EMPTY_USER_ID
    };

    // 验证阈值
    static constexpr size_t MAX_PROMPT_LENGTH = 4096;
    static constexpr int32_t MIN_MAX_TOKENS = 1;
    static constexpr int32_t MAX_MAX_TOKENS = 2048;
    static constexpr float MIN_TEMPERATURE = 0.0f;
    static constexpr float MAX_TEMPERATURE = 2.0f;
    static constexpr float MIN_TOP_P = 0.0f;
    static constexpr float MAX_TOP_P = 1.0f;

    static ValidationResult validate(const recommend::GenerateRequest* request) {
        // 1. 验证 prompt
        if (request->prompt().empty()) {
            return ValidationResult::INVALID_PROMPT;
        }
        if (request->prompt().length() > MAX_PROMPT_LENGTH) {
            return ValidationResult::PROMPT_TOO_LONG;
        }

        // 2. 验证 max_tokens
        if (request->max_tokens() < MIN_MAX_TOKENS || 
            request->max_tokens() > MAX_MAX_TOKENS) {
            return ValidationResult::INVALID_MAX_TOKENS;
        }

        // 3. 验证 temperature
        float temp = request->temperature();
        if (std::isnan(temp) || std::isinf(temp) ||
            temp < MIN_TEMPERATURE || temp > MAX_TEMPERATURE) {
            return ValidationResult::INVALID_TEMPERATURE;
        }

        // 4. 验证 top_p
        float top_p = request->top_p();
        if (std::isnan(top_p) || std::isinf(top_p) ||
            top_p < MIN_TOP_P || top_p > MAX_TOP_P) {
            return ValidationResult::INVALID_TOP_P;
        }

        return ValidationResult::VALID;
    }

    static std::string to_error_message(ValidationResult result) {
        switch (result) {
            case ValidationResult::INVALID_PROMPT:
                return "Prompt is empty or too long (max: 4096)";
            case ValidationResult::INVALID_MAX_TOKENS:
                return "max_tokens must be between 1 and 2048";
            case ValidationResult::INVALID_TEMPERATURE:
                return "temperature must be between 0.0 and 2.0";
            case ValidationResult::INVALID_TOP_P:
                return "top_p must be between 0.0 and 1.0";
            default:
                return "Unknown validation error";
        }
    }
};
```

#### 使用示例

```cpp
auto validation_result = RequestValidator::validate(request);
if (validation_result != RequestValidator::ValidationResult::VALID) {
    response->set_error_code(400);
    response->set_error_message(RequestValidator::to_error_message(validation_result));
    
    LOG(WARNING) << "trace_id=" << request->trace_id()
                << ", validation_failed: " 
                << RequestValidator::to_error_message(validation_result);
    return;
}
```

#### 优势
- ✅ 防止非法请求
- ✅ 提前发现错误
- ✅ 清晰的错误提示
- ✅ 400 Bad Request 响应

---

### 4. 并发处理 - 线程池 ⭐⭐⭐⭐

#### 优化方案
**使用线程池处理耗时任务**

#### 设计思路
- BRPC 工作线程只负责接收请求和返回响应
- 耗时的 vLLM 调用提交到线程池异步处理
- 避免阻塞 BRPC 工作线程

#### 实现方案

```cpp
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });
                        
                        if (stop_ && tasks_.empty()) return;
                        
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    template<typename F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using return_type = decltype(f());
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::forward<F>(f));
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace([task]() { (*task)(); });
        }
        
        condition_.notify_one();
        return result;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_ = false;
};

// 在服务中使用
class RecommendServiceImpl {
public:
    RecommendServiceImpl() : thread_pool_(4) {}  // 4 个工作线程

    void Generate(...) {
        // 提交到线程池
        auto future = thread_pool_.submit([this, request]() {
            return process_vllm_request(request);
        });
        
        // 等待结果
        auto result = future.get();
        
        // 填充响应
        fill_response(result, response);
    }

private:
    ThreadPool thread_pool_;
};
```

#### 配置参数
```bash
--thread_pool_size=4              # 线程池大小
--max_concurrent_requests=100     # 最大并发请求数
```

#### 优势
- ✅ 提升并发能力
- ✅ 不占用 BRPC 工作线程
- ✅ 资源可控
- ⚠️ 增加代码复杂度

---

### 5. 错误处理 - 完善错误码和降级 ⭐⭐⭐

#### 优化方案
**区分错误类型，添加降级策略**

#### 错误码设计

```cpp
enum class ErrorCode {
    // 成功
    SUCCESS = 0,
    
    // 客户端错误 (4xx)
    VALIDATION_ERROR = 400,        // 参数验证失败
    INVALID_REQUEST = 400,         // 请求格式错误
    
    // 服务端错误 (5xx)
    VLLM_SERVICE_ERROR = 502,      // vLLM 服务错误
    VLLM_TIMEOUT = 504,            // vLLM 超时
    INTERNAL_ERROR = 500,          // 内部错误
    SERVICE_BUSY = 503,            // 服务繁忙
    PARSE_ERROR = 500              // 解析错误
};
```

#### 降级策略

```cpp
// 1. vLLM 超时降级
if (http_cntl.ErrorCode() == ETIMEDOUT) {
    response->set_error_code(504);
    response->set_error_message("vLLM service timeout");
    
    // 可选：返回缓存结果或默认结果
    // response->set_generated_text(get_default_response());
    return;
}

// 2. 并发过高降级
if (concurrent_requests_ > MAX_CONCURRENT) {
    response->set_error_code(503);
    response->set_error_message("Service busy, please try again later");
    return;
}

// 3. 重试机制
int retry_count = 0;
const int MAX_RETRY = 2;

while (retry_count < MAX_RETRY) {
    auto result = call_vllm(request);
    if (result.success) {
        return result;
    }
    
    if (!result.is_retryable) {
        return result;  // 不可重试的错误直接返回
    }
    
    retry_count++;
    std::this_thread::sleep_for(std::chrono::milliseconds(100 * retry_count));
}
```

---

### 6. 客户端优化 - 连接池和重试 ⭐⭐

#### 优化方案
**使用 BRPC 内置连接池和重试机制**

#### 连接池配置

```cpp
brpc::Channel channel;
brpc::ChannelOptions options;

// 连接池配置
options.connection_type = "pooled";  // 使用连接池
options.max_connection_pool_size = 10;  // 最大连接数

// 超时配置
options.timeout_ms = 10000;  // 10 秒超时

// 重试配置
options.retry_count = 2;  // 失败重试 2 次

if (channel.Init("127.0.0.1:8001", &options) != 0) {
    LOG(ERROR) << "Failed to initialize channel";
    return -1;
}
```

---

## 📊 优化优先级和计划

### Phase 1 (P0 - 必须实现)
1. ✅ **集成 RapidJSON** - 替换手写 JSON 函数
2. ✅ **添加请求验证** - 参数校验保护
3. ✅ **实现 trace_id 时延记录** - 配合探针采集

### Phase 2 (P1 - 重要优化)
4. ⚠️ **添加线程池** - 提升并发能力
5. ⚠️ **完善错误处理** - 错误码和降级策略

### Phase 3 (P2 - 可选优化)
6. ⚠️ **客户端优化** - 连接池和重试

---

## 🎯 预期效果

| 指标 | 优化前 | 优化后 (P0+P1) | 提升 |
|------|--------|----------------|------|
| JSON 解析性能 | 手写函数 | RapidJSON | **10x+** |
| 并发能力 | 低 | 线程池 | **5-10x** |
| 安全性 | 无验证 | 完善验证 | **质的飞跃** |
| 时延可观测性 | 仅总延迟 | 分阶段延迟 | **全面分析** |
| 错误处理 | 简单 | 完善 + 降级 | **可靠性提升** |

---

## 📝 下一步行动

1. **删除之前创建的工具类**（thread_pool.h, json_utils.h 等）
2. **安装 RapidJSON** 库
3. **实现 P0 优化**：
   - 集成 RapidJSON
   - 添加请求验证
   - 实现 trace_id 时延记录
4. **测试验证**
5. **根据需求决定是否实现 P1、P2**

---

## 🔧 依赖安装

### RapidJSON
```bash
# Ubuntu/Debian
sudo apt-get install rapidjson-dev

# 源码安装
git clone https://github.com/Tencent/rapidjson.git
cd rapidjson
mkdir build && cd build
cmake ..
make
sudo make install
```

### CMakeLists.txt 更新
```cmake
find_package(RapidJSON REQUIRED)

target_include_directories(your_target PRIVATE
    ${RAPIDJSON_INCLUDE_DIRS}
)

target_link_libraries(your_target PRIVATE
    ${RAPIDJSON_LIBRARIES}
)
```

---

## ❓ 待确认事项

1. **trace_id 字段**：proto 中是否需要添加 `trace_id` 字段？
2. **探针接口**：轻量级探针的具体接口和上报方式？
3. **线程池大小**：是否需要根据 CPU 核心数动态调整？
4. **降级策略**：vLLM 失败时是否需要返回默认结果？

请您审阅这个优化方案，确认无误后我再开始实施！
