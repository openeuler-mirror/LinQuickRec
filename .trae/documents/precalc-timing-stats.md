# PrecalcService 时延统计计划

## 目标

在 PrecalcService 的客户端和服务端添加详细的时延统计，分别测量：
1. **Client → Server 网络时延**：request 从客户端发出到服务端接收
2. **KVCache 写入时延**：写入元戎 KVWorker 的时间
3. **Server → Client 网络时延**：response 从服务端发出到客户端接收

## 实现方案

### 服务端（precalc_server.cpp）

#### 1. 添加时延统计点

在 `Precalculate` 方法中：

```cpp
void Precalculate(const precalc::PrecalcRequest* request,
                  precalc::PrecalcResponse* response,
                  google::protobuf::Closure* done) override {
    
    brpc::ClosureGuard done_guard(done);
    
    // 时间点 1: 服务端收到请求
    int64_t server_receive_us = butil::gettimeofday_us();
    
    // ... 处理请求 ...
    
    // 时间点 2: 开始写入 KVWorker
    int64_t kvwrite_start_us = butil::gettimeofday_us();
    
    // 写入 KVWorker
    status = kv_client.Create(...);
    status = kv_client.Set(...);
    
    // 时间点 3: KVWorker 写入完成
    int64_t kvwrite_end_us = butil::gettimeofday_us();
    int64_t kvwrite_cost_us = kvwrite_end_us - kvwrite_start_us;
    
    // ... 生成 payload ...
    
    // 时间点 4: 服务端发送响应
    int64_t server_send_us = butil::gettimeofday_us();
    
    // 计算时延
    int64_t server_process_us = server_send_us - server_receive_us;
    
    // 打印日志
    LOG(INFO) << "Precalculate timing breakdown:"
              << " kvwrite_cost=" << kvwrite_cost_us / 1000.0 << " ms"
              << " server_process_total=" << server_process_us / 1000.0 << " ms";
}
```

#### 2. 配置参数

添加时延统计的开关：

```cpp
DEFINE_bool(enable_timing_stats, true, "是否启用详细时延统计");
```

### 客户端（precalc_test_client.cpp）

#### 1. 添加时延统计点

```cpp
int main(int argc, char* argv[]) {
    // ... 初始化 ...
    
    // 时间点 1: 客户端发送请求
    int64_t client_send_us = butil::gettimeofday_us();
    
    // 发送 RPC 请求
    stub.Precalculate(&cntl, &request, &response, nullptr);
    
    // 时间点 2: 客户端收到响应
    int64_t client_receive_us = butil::gettimeofday_us();
    
    // 总时延
    int64_t total_latency_us = client_receive_us - client_send_us;
    
    // 从服务端获取时延信息（通过 response 或日志）
    // 或者通过 cntl 获取网络时延
    
    // 打印日志
    LOG(INFO) << "Precalc client timing breakdown:"
              << " total_latency=" << total_latency_us / 1000.0 << " ms"
              << " network_latency=" << cntl.latency_us() / 1000.0 << " ms";
}
```

#### 2. 使用 BRPC 内置的时延统计

BRPC 的 Controller 提供了时延信息：

```cpp
// 获取网络时延（RTT）
int64_t network_latency_us = cntl.latency_us();

// 获取服务端处理时延（如果服务端返回）
// 需要在 response 中添加 timing 信息
```

### 方案选择

#### 方案 A：简单日志统计（推荐）

**优点**：
- 实现简单
- 不影响现有接口
- 日志清晰易读

**实现**：
- 服务端记录各个阶段的时延
- 客户端记录总时延和网络时延
- 通过日志分析各部分时延

#### 方案 B：在 Response 中添加时延信息

**优点**：
- 客户端可以获取详细的时延 breakdown
- 便于端到端分析

**缺点**：
- 需要修改 proto 文件
- 增加响应大小

**实现**：
```protobuf
message PrecalcResponse {
    string user_feat_key = 1;
    string payload = 2;
    
    // 时延统计信息（微秒）
    int64 kvwrite_latency_us = 3;
    int64 server_process_latency_us = 4;
}
```

### 推荐方案

采用**方案 A（简单日志统计）**，原因：
1. 当前需求只是打印到日志
2. 实现简单，不需要修改 proto
3. 可以快速验证和调试

如果后续需要客户端获取详细时延，再考虑方案 B。

## 实施步骤

### 步骤 1：修改 precalc_server.cpp

1. 添加时延统计配置参数
2. 在 `Precalculate` 方法中添加时延统计点：
   - 服务端收到请求时间
   - KVWorker 写入开始时间
   - KVWorker 写入结束时间
   - 服务端发送响应时间
3. 打印详细的时延 breakdown 日志

### 步骤 2：修改 precalc_test_client.cpp

1. 添加时延统计代码：
   - 客户端发送请求时间
   - 客户端收到响应时间
2. 打印时延日志：
   - 总时延
   - 网络时延（使用 cntl.latency_us()）

### 步骤 3：验证和测试

1. 编译代码
2. 启动服务
3. 运行客户端测试
4. 检查日志中的时延信息

## 日志输出示例

### 服务端日志

```
I0101 12:00:00.123456] Precalculate request received
I0101 12:00:00.125000] Generated user_feat_key: 12345_1704067200123456
I0101 12:00:00.130000] Generated precalc result with size: 8912896 bytes (8.5 MB)
I0101 12:00:00.150000] Precalc result written to KVWorker: key=12345_1704067200123456
I0101 12:00:00.155000] Precalculate timing breakdown:
    kvwrite_cost=20.0 ms
    server_process_total=31.544 ms
I0101 12:00:00.155100] Precalculate completed, cost=31.644 ms
```

### 客户端日志

```
I0101 12:00:00.100000] Sending request to 127.0.0.1:8004
I0101 12:00:00.135000] RPC completed
I0101 12:00:00.135100] Client timing breakdown:
    total_latency=35.1 ms
    network_latency=30.5 ms
    server_process_time=31.5 ms (from server log)
```

## 代码修改详情

### precalc_server.cpp

```cpp
// 添加配置参数
DEFINE_bool(enable_timing_stats, true, "是否启用详细时延统计");

// 在 Precalculate 方法中
void Precalculate(...) {
    int64_t server_receive_us = butil::gettimeofday_us();
    
    // ... 请求验证 ...
    
    // KVWorker 写入时延
    int64_t kvwrite_start_us = butil::gettimeofday_us();
    
    Status status = kv_client.Create(...);
    Status status = kv_client.Set(...);
    
    int64_t kvwrite_end_us = butil::gettimeofday_us();
    int64_t kvwrite_cost_us = kvwrite_end_us - kvwrite_start_us;
    
    // ... 生成 payload ...
    
    int64_t server_send_us = butil::gettimeofday_us();
    int64_t server_process_us = server_send_us - server_receive_us;
    
    if (FLAGS_enable_timing_stats) {
        LOG(INFO) << "Precalculate timing breakdown:"
                  << " kvwrite_cost=" << kvwrite_cost_us / 1000.0 << " ms"
                  << " server_process_total=" << server_process_us / 1000.0 << " ms";
    }
}
```

### precalc_test_client.cpp

```cpp
int main(...) {
    // ... 初始化 ...
    
    int64_t client_send_us = butil::gettimeofday_us();
    
    stub.Precalculate(&cntl, &request, &response, nullptr);
    
    int64_t client_receive_us = butil::gettimeofday_us();
    int64_t total_latency_us = client_receive_us - client_send_us;
    
    LOG(INFO) << "Client timing breakdown:"
              << " total_latency=" << total_latency_us / 1000.0 << " ms"
              << " network_latency=" << cntl.latency_us() / 1000.0 << " ms";
}
```

## 注意事项

1. **时间单位**：统一使用微秒（us），日志输出时转换为毫秒（ms）
2. **日志级别**：使用 INFO 级别，便于调试
3. **性能影响**：时延统计对性能影响很小（只有几次时间戳获取）
4. **可配置**：通过 `--enable_timing_stats` 参数控制是否启用
