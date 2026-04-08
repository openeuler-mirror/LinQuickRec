# BRPC 服务端集成 vLLM 服务实现计划

## 1. 项目现状分析

当前项目结构：

* `proto/gateway.proto`：定义了 `vllm.gateway` 命名空间下的服务

* `server/brpc_server.cpp`：实现了 `GatewayServiceImpl`，目前是 Echo 模式

* `client/brpc_client.cpp`：BRPC 客户端

* `CMakeLists.txt`：构建配置

## 2. 修改目标

将服务端程序更改为：

1. 使用 `recommend::RecommendService` 服务定义
2. 实现 `RecommendServiceImpl` 类，继承自 `recommend::RecommendService`
3. 实现 `Generate` 方法，调用外部 vLLM 服务
4. 添加辅助函数：

   * `escape_json_string`：转义 JSON 字符串

   * `extract_json_string`：从 JSON 中提取字符串值

   * `extract_json_int`：从 JSON 中提取整数值

## 3. 详细修改步骤

### 步骤 1：更新 proto 文件

修改 `proto/gateway.proto`：

* 更改命名空间为 `recommend`

* 定义新的 `RecommendService` 服务

* 添加 `Generate` 方法的请求/响应消息

* 保持 `HealthCheck` 服务（可选）

### 步骤 2：实现辅助函数

在 `server/brpc_server.cpp` 中添加：

* `escape_json_string`：处理 JSON 特殊字符转义

* `extract_json_string`：解析 JSON 字符串并提取指定键的值

* `extract_json_int`：解析 JSON 字符串并提取指定键的整数值

### 步骤 3：实现 vLLM 服务客户端

添加 `vLLMClient` 类：

* 封装与外部 vLLM 服务的 HTTP/REST 通信

* 处理请求构建、发送和响应解析

* 支持超时和错误处理

### 步骤 4：实现 RecommendServiceImpl

创建 `RecommendServiceImpl` 类：

* 继承自 `recommend::RecommendService`

* 实现 `Generate` 方法：

  1. 解析客户端请求
  2. 构建 vLLM 服务请求
  3. 调用 vLLM 服务
  4. 解析 vLLM 响应
  5. 构建并返回客户端响应

* 实现 `HealthCheck` 方法（可选）

### 步骤 5：更新主函数

修改 `main` 函数：

* 创建 `RecommendServiceImpl` 实例

* 注册到 BRPC 服务器

* 启动服务器

### 步骤 6：更新 CMakeLists.txt

确保构建配置正确：

* 包含必要的头文件路径

* 链接所需的库（如 curl 用于 HTTP 通信）

* 正确生成 protobuf 代码

## 4. 技术实现细节

### 4.1 vLLM 服务集成

使用 HTTP POST 请求与 vLLM 服务通信：

* 端点：`http://<vllm-host>:<port>/generate`

* 请求格式：JSON

* 响应格式：JSON

### 4.2 错误处理

* 网络错误：超时、连接失败

* vLLM 服务错误：HTTP 错误码、JSON 解析错误

* 业务错误：参数验证失败

### 4.3 性能考虑

* 使用线程池处理并发请求

* 合理设置 HTTP 请求超时

* 避免阻塞主线程

## 5. 依赖项

* BRPC 框架

* Protobuf

* cURL 库（用于 HTTP 通信）

* 标准 C++ 库

## 6. 风险评估

| 风险         | 影响   | 缓解措施             |
| ---------- | ---- | ---------------- |
| vLLM 服务不可用 | 服务降级 | 实现健康检查和错误处理      |
| 网络延迟       | 响应缓慢 | 设置合理超时，监控性能      |
| JSON 解析错误  | 服务异常 | 健壮的 JSON 解析和错误处理 |
| 并发请求过高     | 系统负载 | 线程池配置，请求限流       |

## 7. 测试计划

1. 单元测试：辅助函数测试
2. 集成测试：与 vLLM 服务的通信
3. 性能测试：并发请求处理
4. 故障测试：vLLM 服务不可用时的处理

## 8. 预期成果

* 完整的 BRPC 服务器实现

* 与外部 vLLM 服务的集成

* 健壮的错误处理机制

* 可配置的服务参数

* 详细的日志记录

