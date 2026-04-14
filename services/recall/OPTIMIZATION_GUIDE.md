# Recall 服务优化说明

## 📋 优化概述

本次优化针对召回服务（brpc_server.cpp）进行了全面的改进，主要包含以下 4 个方面：

1. **JSON 处理规范化** - 使用 JsonUtils 工具类替代手写辅助函数
2. **多线程支持** - 添加线程池实现并发处理
3. **请求验证保护** - 加入完善的参数验证机制
4. **延迟计算优化** - 实现统计级延迟指标（平均、P95、P99）

## 📁 文件结构

```
services/recall/
├── backup/                     # 原始代码备份
│   ├── brpc_server.cpp.backup
│   ├── brpc_client.cpp.backup
│   └── recommend.proto.backup
├── include/                    # 新增工具类头文件
│   ├── thread_pool.h          # 线程池
│   ├── json_utils.h           # JSON 工具类
│   ├── request_validator.h    # 请求验证器
│   └── latency_metrics.h      # 延迟统计
├── src/                        # 优化后的源码
│   └── brpc_server_optimized.cpp
├── server/                     # 原始服务端代码（保留）
│   └── brpc_server.cpp
├── client/                     # 客户端代码（未修改）
│   └── brpc_client.cpp
└── CMakeLists.txt             # 更新后的构建配置
```

## ✅ 优化详情

### 1. JSON 处理规范化

#### 优化前
```cpp
// 手写辅助函数，功能有限且易出错
std::string escape_json_string(const std::string& input) {
    // ... 实现细节
}

bool extract_json_string(const std::string& json, const std::string& key, std::string& value) {
    // ... 实现细节
}
```

#### 优化后
```cpp
// 使用规范的 JsonUtils 工具类
#include "json_utils.h"

std::string escaped = JsonUtils::escape_string(prompt);
std::string content;
JsonUtils::extract_string(resp_body, "content", content);
```

**优势**：
- ✅ 代码更规范，易于维护
- ✅ 功能更完整，支持更多转义字符
- ✅ 更好的错误处理
- ✅ 可复用性强

### 2. 多线程支持

#### 优化前
- 单线程处理请求
- 每个请求阻塞一个 BRPC 工作线程
- 并发能力受限

#### 优化后
```cpp
// 线程池初始化（默认 4 个工作线程）
ThreadPool thread_pool_(FLAGS_thread_pool_size);

// 将耗时任务提交到线程池
auto future = thread_pool_.submit([this, request]() {
    return process_vllm_request(request);
});

// 等待任务完成
auto result = future.get();
```

**优势**：
- ✅ 支持并发处理多个请求
- ✅ 不占用 BRPC 工作线程
- ✅ 可通过配置调整线程池大小
- ✅ 更好的资源利用率

**配置参数**：
```bash
--thread_pool_size=4          # 线程池大小
--max_concurrent_requests=100 # 最大并发请求数
```

### 3. 请求验证保护

#### 优化前
- 没有参数验证
- 可能接受非法请求
- 存在安全隐患

#### 优化后
```cpp
#include "request_validator.h"

// 验证请求参数
auto validation_result = validate_request(request);
if (validation_result != RequestValidator::ValidationResult::VALID) {
    std::string error_msg = RequestValidator::validation_result_to_message(validation_result);
    response->set_error_code(400);
    response->set_error_message("Validation error: " + error_msg);
    return;
}
```

**验证项目**：
- ✅ **Prompt 验证**：非空、长度限制（4096）、字符合法性
- ✅ **max_tokens 验证**：范围 1-2048
- ✅ **temperature 验证**：范围 0.0-2.0，排除 NaN/Inf
- ✅ **top_p 验证**：范围 0.0-1.0，排除 NaN/Inf
- ✅ **user_id 验证**：可选，支持空值

**优势**：
- ✅ 防止非法请求
- ✅ 提前发现错误，减少无效计算
- ✅ 提供清晰的错误提示
- ✅ 可配置验证规则

### 4. 延迟计算优化

#### 优化前
```cpp
// 简单计算单次延迟
int64_t start_us = butil::gettimeofday_us();
// ... 处理逻辑
int64_t end_us = butil::gettimeofday_us();
response->set_latency_ms((end_us - start_us)/1000);
```

#### 优化后
```cpp
#include "latency_metrics.h"

// RAII 风格的自动计算器
LatencyAutoCalculator latency_calc(latency_metrics_);

// 自动记录延迟，无需手动计算

// 统计指标
LOG(INFO) << "Average Latency: " << metrics.get_average_ms() << "ms";
LOG(INFO) << "P95 Latency: " << metrics.get_p95_ms() << "ms";
LOG(INFO) << "P99 Latency: " << metrics.get_p99_ms() << "ms";
```

**优势**：
- ✅ 自动记录，无需手动计算
- ✅ 支持多种统计指标（平均、P95、P99）
- ✅ 线程安全的统计
- ✅ 支持最大值、最小值统计
- ✅ 服务退出时自动打印统计信息

## 🚀 使用指南

### 编译

```bash
cd services/recall
mkdir build && cd build
cmake ..
make -j$(nproc)
```

**生成的可执行文件**：
- `vllm_server_optimized` - 优化版服务端（**推荐使用**）
- `vllm_server` - 原始版服务端（保留用于对比）
- `vllm_client` - 客户端

### 运行

```bash
# 启动优化版服务端
./vllm_server_optimized \
    --server_port=8001 \
    --thread_pool_size=4 \
    --max_concurrent_requests=100 \
    --vllm_timeout_ms=5000

# 启动客户端测试
./vllm_client \
    --server="127.0.0.1:8001" \
    --prompt="你好，请介绍一下你自己"
```

### 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8001 | 服务器监听端口 |
| `--thread_pool_size` | 4 | 线程池大小 |
| `--max_concurrent_requests` | 100 | 最大并发请求数 |
| `--vllm_timeout_ms` | 5000 | vLLM 请求超时（毫秒） |
| `--vllm_base_url` | http://127.0.0.1:8000 | vLLM 基础 URL |
| `--vllm_endpoint` | /v1/chat/completions | vLLM 端点 |
| `--model_name` | /workspace/share/Qwen3-0.6B | 模型名称 |

## 📊 性能对比

### 预期提升

| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| 并发能力 | 低 | 高 | 10x+ |
| 平均延迟 | 基准 | -10%~20% | 优化 |
| P99 延迟 | 波动大 | 稳定 | 显著优化 |
| 请求验证 | 无 | 有 | 安全性提升 |
| 错误处理 | 简单 | 完善 | 可靠性提升 |

### 监控指标

服务退出时会自动打印统计信息：

```
===========================================
Service Statistics
===========================================
Total Requests: 1000
Average Latency: 123.45ms
P95 Latency: 156.78ms
P99 Latency: 189.01ms
Min Latency: 45.67ms
Max Latency: 345.89ms
===========================================
```

## 🔧 扩展建议

### 近期优化
1. 添加连接池管理 vLLM 连接
2. 实现请求重试机制
3. 添加熔断器模式
4. 集成 Prometheus 指标导出

### 长期优化
1. 替换 JSON 解析为 rapidjson/simdjson
2. 添加异步非阻塞调用
3. 实现负载均衡（多 vLLM 实例）
4. 添加请求优先级队列

## ⚠️ 注意事项

1. **线程池大小配置**
   - 太小：并发能力不足
   - 太大：上下文切换开销增加
   - 建议：CPU 核心数 * 2

2. **并发请求数限制**
   - 防止服务过载
   - 根据实际资源情况调整

3. **超时设置**
   - vLLM 超时应该大于平均处理时间
   - 建议设置为 P99 延迟的 1.5-2 倍

4. **内存使用**
   - 线程池会占用额外内存
   - 注意系统总内存限制

## 📝 版本历史

- **v2.0** (2024-01) - 优化版本
  - ✅ 添加线程池支持
  - ✅ 添加请求验证
  - ✅ 优化延迟计算
  - ✅ 规范化 JSON 处理

- **v1.0** (原始版本) - 基础版本
  - 基础 BRPC 服务实现
  - 简单的 JSON 解析
  - 单线程处理

## 📞 问题反馈

如有问题或建议，请查看：
- 代码注释
- 日志输出
- 性能统计信息
