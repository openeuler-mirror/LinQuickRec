# Proxy 网关服务详细设计方案

## 1. 设计目标

### 1.1 核心目标

- **统一网关入口**：作为系统唯一对外暴露的 HTTP 端点，接收外部推荐请求
- **请求编排**：按业务逻辑依次调用 Feature → {Recall ∥ Precalc} → Rank 四个下游服务
- **错误隔离**：单一下游故障不应导致整个请求失败，支持部分失败降级
- **可观测性**：记录各阶段时延，支持全链路追踪

### 1.2 设计原则

- **零额外依赖**：复用项目已有的 BRPC + common_lib，不引入新的 HTTP 框架
- **与现有模式一致**：连接管理、并发模型、日志风格与 RankMaster 等已完成服务保持一致
- **非侵入式**：Proxy 只做编排聚合，不修改下游请求/响应结构
- **配置驱动**：所有下游地址、超时等通过 gflags 配置，无需重新编译

## 2. 系统架构

### 2.1 整体架构

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Proxy 服务进程                                 │
│                                                                      │
│  ┌──────────────────────┐   ┌────────────────────────────────────┐   │
│  │   BRPC HTTP Server   │   │        全局线程池                   │   │
│  │   (0.0.0.0:8080)     │   │  (common::get_global_thread_pool)  │   │
│  │                      │   │                                    │   │
│  │  POST /Proxy/Recommend──┼──▶ submit(process_recommend_request)│   │
│  └──────────────────────┘   └──────────┬─────────────────────────┘   │
│                                        │                             │
│  ┌─────────────────────────────────────▼──────────────────────────┐  │
│  │                   Request Processing Pipeline                   │  │
│  │                                                                  │  │
│  │  ┌──────────┐    ┌──────────────────┐    ┌───────────┐          │  │
│  │  │ Stage 1  │───▶│  Stage 2         │───▶│ Stage 3   │          │  │
│  │  │ Feature  │    │ Recall ∥ Precalc │    │ Rank      │          │  │
│  │  └──────────┘    └──────────────────┘    └───────────┘          │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │                    BRPC Channel Pool                            │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐          │  │
│  │  │ Feature  │ │ Recall   │ │ Precalc  │ │ Rank     │          │  │
│  │  │ :8003    │ │ :8001    │ │ :8004    │ │ :8005    │          │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘          │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2 目录结构

```
services/proxy/
├── CMakeLists.txt
├── Dockerfile
├── README.md              # 服务概述和业务流程
├── DESIGN.md              # 本文档 - 详细设计
├── server/
│   ├── include/
│   │   └── proxy_server.h   # ProxyServiceImpl 声明
│   └── src/
│       ├── main.cpp            # 入口：BRPC 服务器初始化
│       └── proxy_server.cpp  # 服务实现：业务编排逻辑
└── client/
    └── proxy_test_client.cpp # 测试客户端（HTTP 调用）
```

### 2.3 核心组件

| 组件 | 职责 |
|------|------|
| `ProxyServiceImpl` | BRPC 服务实现类，实现 `proxy::Proxy` 接口的 `Recommend` 方法 |
| `process_recommend_request()` | 核心编排逻辑：依次/并行调用 4 个下游服务 |
| `call_feature_service()` | 封装 FeatureService 的 BRPC 调用 |
| `call_recall_service()` | 封装 RecallService 的 BRPC 调用 |
| `call_precalc_service()` | 封装 PrecalcService 的 BRPC 调用 |
| `call_rank_service()` | 封装 RankServiceMaster 的 BRPC 调用 |

## 3. 详细设计

### 3.1 服务器层

Proxy 使用 **BRPC 内建 HTTP 支持** 对外提供服务。BRPC 的 `brpc::Server` 原生支持 HTTP 协议——当客户端通过 HTTP POST 请求 `/Proxy/Recommend` 时，BRPC 自动将 HTTP Body（JSON/Protobuf）反序列化为 `RecommendRequest` 并路由到 `Recommend` 方法。

```cpp
// main.cpp 关键逻辑
int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    // 初始化全局线程池
    common::get_global_thread_pool();

    // 创建服务实例
    proxy::ProxyServiceImpl service_impl;

    // 创建 BRPC 服务器
    brpc::Server server;

    // 添加服务（BRPC HTTP 模式自动支持 HTTP POST）
    server.AddService(&service_impl, brpc::SERVER_DOESNT_OWN_SERVICE);

    // 启动监听
    server.Start("0.0.0.0:8080", nullptr);
    server.RunUntilAskedToQuit();
}
```

**HTTP 调用示例：**

```bash
curl -X POST "http://127.0.0.1:8080/Proxy/Recommend" \
  -H "Content-Type: application/json" \
  -d '{"user_id": 12345, "payload": "test"}'
```

BRPC 会：
1. 识别 HTTP Method + Path 匹配到 `Proxy::Recommend`
2. 将 JSON Body 通过 Protobuf JSON Parse 反序列化为 `RecommendRequest`
3. 调用 C++ 实现
4. 将 `RecommendResponse` 序列化为 JSON 返回

### 3.2 请求处理管线

```
Recommend(request, response, done)
  │
  ├─▶ 提交到全局线程池
  │    │
  │    └─▶ process_recommend_request(request, response)
  │         │
  │         │  [Stage 1] 获取特征（同步）
  │         ├─▶ call_feature_service(request, user_feat_out)
  │         │    │
  │         │    │  如果失败 → 设置错误码，直接返回
  │         │    │
  │         │    ▼
  │         │  [Stage 2] 召回 + 预计算（并行）
  │         ├─▶ std::async(call_recall_service, request, recall_rsp_out)
  │         ├─▶ std::async(call_precalc_service, user_feat, precalc_rsp_out)
  │         │    │
  │         │    │  等待两个 future 完成
  │         │    │
  │         │    ▼
  │         │  [Stage 3] 精排（同步）
  │         └─▶ call_rank_service(recall_rsp.sku_ids, precalc_rsp.user_feat_key, response)
  │              │
  │              如果失败 → 设置错误码
  │
  └─▶ ClosureGuard 自动调用 done->Run()
```

### 3.3 Stage 1: 特征获取

**调用方式**：同步阻塞。必须在获取用户特征后才能进行后续的召回和预计算。

```cpp
bool ProxyServiceImpl::call_feature_service(
    const RecommendRequest* request,
    UserFeatureResponse* response) {

    feature::UserFeatureRequest feat_req;
    feat_req.set_user_id(request->user_id());
    // ... 填充特征请求参数

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_feature_timeout_ms);

    feature::FeatureService_Stub stub(feature_channel_.get());
    stub.GetUserFeatures(&cntl, &feat_req, response, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << "FeatureService call failed: " << cntl.ErrorText();
        return false;
    }
    return true;
}
```

**超时处理**：`--feature_timeout_ms` 默认为 3000ms。超时或失败时直接终结本次推荐请求，返回 502。

### 3.4 Stage 2: 并行召回 & 预计算

**调用方式**：`std::async(std::launch::async)` 发起两个并发任务，使用 `future.get()` 同步等待两者完成。

```cpp
// 并行发起
auto recall_future = std::async(std::launch::async, [this, &request]() {
    recall::RecallResponse rsp;
    bool ok = call_recall_service(request, &rsp);
    return std::make_pair(ok, rsp);
});

auto precalc_future = std::async(std::launch::async, [this, &user_feat]() {
    precalc::PrecalcResponse rsp;
    bool ok = call_precalc_service(user_feat, &rsp);
    return std::make_pair(ok, rsp);
});

// 等待两者完成
auto [recall_ok, recall_rsp] = recall_future.get();
auto [precalc_ok, precalc_rsp] = precalc_future.get();
```

**部分失败策略**：
- Recall 失败但 Precalc 成功 → 记录错误日志，不继续 Rank 阶段，返回空结果
- Precalc 失败但 Recall 成功 → 记录错误日志，不继续 Rank 阶段（Rank 依赖 precalc 的 user_feat_key）
- 两者都失败 → 返回 502

**注意**：RankServiceMaster 同时依赖 Recall 的 sku_ids 和 Precalc 的 user_feat_key，因此两者中任一失败都无法继续进行精排阶段。

### 3.5 Stage 3: 精排

**调用方式**：同步阻塞。将 Stage 2a 返回的 `sku_ids` 和 Stage 2b 返回的 `user_feat_key` 拼接为 `RankRequest` 发送给 RankServiceMaster。

```cpp
bool ProxyServiceImpl::call_rank_service(
    const recall::RecallResponse& recall_rsp,
    const precalc::PrecalcResponse& precalc_rsp,
    RecommendResponse* response) {

    rank::RankMasterRequest rank_req;
    rank_req.set_user_feat_key(precalc_rsp.user_feat_key());

    // 将 sku_ids 序列化为 6 位定长字符串（与 RankMaster 约定格式一致）
    std::string skus_str;
    for (uint64_t sku_id : recall_rsp.sku_ids()) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%06lu", sku_id);
        skus_str += buf;
    }
    rank_req.set_skus(skus_str);

    brpc::Controller cntl;
    cntl.set_timeout_ms(FLAGS_rank_timeout_ms);

    rank::RankMasterService_Stub stub(rank_channel_.get());
    rank::RankMasterResponse rank_rsp;
    stub.Rank(&cntl, &rank_req, &rank_rsp, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << "RankService call failed: " << cntl.ErrorText();
        return false;
    }

    // 将 ranked candidates 写入最终响应
    for (uint64_t cand : rank_rsp.candidates()) {
        response->add_candidates(cand);
    }
    return true;
}
```

### 3.6 连接管理

每个下游服务对应一个 `std::unique_ptr<brpc::Channel>`，在 `ProxyServiceImpl` 构造函数中初始化一次，整个生命周期复用。

```cpp
class ProxyServiceImpl : public proxy::Proxy {
public:
    ProxyServiceImpl() {
        // 初始化 4 个下游 Channel
        init_channel(feature_channel_, FLAGS_feature_service_addr);
        init_channel(recall_channel_, FLAGS_recall_service_addr);
        init_channel(precalc_channel_, FLAGS_precalc_service_addr);
        init_channel(rank_channel_, FLAGS_rank_service_addr);
    }

private:
    bool init_channel(std::unique_ptr<brpc::Channel>& ch, const std::string& addr) {
        ch = std::make_unique<brpc::Channel>();
        brpc::ChannelOptions opts;
        opts.timeout_ms = 5000;      // 默认超时
        opts.connection_type = "pooled";  // 连接池
        opts.max_retry = 2;           // 失败重试
        return ch->Init(addr.c_str(), &opts) == 0;
    }

    std::unique_ptr<brpc::Channel> feature_channel_;
    std::unique_ptr<brpc::Channel> recall_channel_;
    std::unique_ptr<brpc::Channel> precalc_channel_;
    std::unique_ptr<brpc::Channel> rank_channel_;
};
```

**Channel 配置要点**：
- `connection_type = "pooled"`：复用 TCP 连接，避免频繁建连
- `max_retry = 2`：网络抖动时自动重试
- 每个下游的调用可通过 `cntl.set_timeout_ms()` 在调用时单独覆盖超时

### 3.7 错误处理

| 场景 | 行为 | HTTP Status |
|------|------|------------|
| Feature 调用失败/超时 | 记录错误，终止处理 | 502 |
| Recall 调用失败/超时 | 记录错误，终止处理 | 502 |
| Precalc 调用失败/超时 | 记录错误，终止处理 | 502 |
| Rank 调用失败/超时 | 记录错误 | 502 |
| 请求参数校验失败 | 记录错误 | 400 |
| 全部成功 | 正常返回 candidates | 200 |

错误码映射：

```cpp
// 来自 common/error.h 的模块定义
enum class ModuleCode : uint8_t {
    GATEWAY = 0x01,  // Proxy 属于网关模块
};

enum class ErrorType : uint8_t {
    SERVICE_ERROR = 0x03,  // 依赖服务错误
    TIMEOUT       = 0x04,  // 超时错误
};
```

### 3.8 并发模型

```
BRPC I/O 线程
    │
    │  Recommend() 被调用（I/O 线程上下文）
    │
    ▼
common::get_global_thread_pool().submit(process_recommend_request)
    │
    │  线程池线程 1 执行 process_recommend_request()
    │
    ├── call_feature_service()              ← 同步阻塞（线程池线程 T1）
    │
    ├── std::async(call_recall_service)     ← 新线程 T2
    ├── std::async(call_precalc_service)    ← 新线程 T3
    │   ├── future.get() wait T2            ← 线程池线程 T1 等待
    │   └── future.get() wait T3            ← 同上
    │
    └── call_rank_service()                 ← 同步阻塞（线程池线程 T1）
```

**线程安全说明**：
- 每个请求在独立的线程池线程中处理，请求间天然隔离
- `std::async` 创建的临时线程仅在 Stage 2 存活，结束后自动回收
- 下游 Channel 是线程安全的（BRPC Channel 是 thread-safe 的）

### 3.9 可观测性

#### 阶段时延统计

使用 `butil::gettimeofday_us()` 记录各阶段耗时：

```cpp
void ProxyServiceImpl::process_recommend_request(
    const RecommendRequest* request,
    RecommendResponse* response) {

    int64_t t0 = butil::gettimeofday_us();

    // Stage 1: Feature
    UserFeatureResponse user_feat;
    bool feat_ok = call_feature_service(request, &user_feat);
    int64_t t1 = butil::gettimeofday_us();

    if (!feat_ok) {
        LOG(ERROR) << "Feature stage failed";
        return;
    }

    // Stage 2: Recall + Precalc in parallel
    auto [recall_ok, recall_rsp] = recall_future.get();
    auto [precalc_ok, precalc_rsp] = precalc_future.get();
    int64_t t2 = butil::gettimeofday_us();

    if (!recall_ok || !precalc_ok) {
        LOG(ERROR) << "Recall/Precalc stage failed";
        return;
    }

    // Stage 3: Rank
    bool rank_ok = call_rank_service(recall_rsp, precalc_rsp, response);
    int64_t t3 = butil::gettimeofday_us();

    if (FLAGS_enable_timing_stats) {
        LOG(INFO) << "[Proxy Timing] "
                   << " feature=" << (t1 - t0) / 1000.0 << "ms"
                   << " recall+precalc=" << (t2 - t1) / 1000.0 << "ms"
                   << " rank=" << (t3 - t2) / 1000.0 << "ms"
                   << " total=" << (t3 - t0) / 1000.0 << "ms";
    }
}
```

#### trace_id 传递

在每个下游请求的 Controller 中设置 `trace_id`：

```cpp
// 生成或传递 trace_id
std::string trace_id = request->payload().empty()
    ? butil::GenerateTraceId()  // 新请求生成
    : request->payload();       // 复用上游 trace_id

// 在调用下游时传递
brpc::Controller cntl;
cntl.set_log_id(std::stoull(trace_id));
// ...
```

### 3.10 与现有服务的集成

Proxy 调用各下游服务时需要处理的消息映射：

| Proxy 输出 | 下游输入 | 说明 |
|-----------|---------|------|
| `request.user_id` → | FeatureService 的 `UserFeatureRequest.user_id` | 用户 ID |
| FeatureService 的 `UserFeatureResponse.user_logs` → | RecallService 的 `RecallRequest.user_logs` | 用户特征日志 |
| FeatureService 的 `UserFeatureResponse.user_feat` → | PrecalcService 的 `PrecalcRequest.user_feat` | 用户特征数据 |
| RecallService 的 `RecallResponse.sku_ids` → | RankService 的 `RankRequest.skus` | 候选 ID 列表 |
| PrecalcService 的 `PrecalcResponse.user_feat_key` → | RankService 的 `RankRequest.user_feat_key` | 预计算结果索引 |

## 4. 配置参数

| 参数名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `--server_port` | int32 | 8080 | Proxy HTTP 服务监听端口 |
| `--feature_service_addr` | string | "127.0.0.1:8003" | FeatureService 地址 |
| `--recall_service_addr` | string | "127.0.0.1:8001" | RecallService 地址 |
| `--precalc_service_addr` | string | "127.0.0.1:8004" | PrecalcService 地址 |
| `--rank_service_addr` | string | "127.0.0.1:8005" | RankServiceMaster 地址 |
| `--feature_timeout_ms` | int32 | 3000 | Feature 调用超时 (ms) |
| `--recall_timeout_ms` | int32 | 5000 | Recall 调用超时 (ms) |
| `--precalc_timeout_ms` | int32 | 5000 | Precalc 调用超时 (ms) |
| `--rank_timeout_ms` | int32 | 10000 | Rank 调用超时 (ms) |
| `--enable_timing_stats` | bool | true | 是否打印阶段时延统计 |

## 5. API 契约

### 5.1 Proto 定义

详见 `proto/proxy.proto`：

```protobuf
service Proxy {
    rpc Recommend(RecommendRequest) returns (RecommendResponse);
}

message RecommendRequest {
    uint64 user_id = 1;
    string payload = 2;  // 附加数据 / trace_id 透传
}

message RecommendResponse {
    repeated uint64 candidates = 1;
}
```

### 5.2 HTTP 映射

| 属性 | 值 |
|------|-----|
| Method | `POST` |
| Path | `/Proxy/Recommend` |
| Content-Type | `application/json` 或 `application/proto` |
| 请求 Body | `RecommendRequest` 的 JSON 序列化 |
| 响应 Body | `RecommendResponse` 的 JSON 序列化 |

## 6. 性能分析

### 6.1 时延预算

假设各下游 P99 时延如下（基于现有服务实现推断）：

| 阶段 | 估算 P99 时延 | 说明 |
|------|--------------|------|
| Feature | ~50ms | Redis 读取（未实现，保守估算） |
| Recall | ~200ms | vLLM 推理 + 网络 |
| Precalc | ~100ms | KVWorker 写入 + 随机数据生成 |
| Rank | ~500ms | 子图并行 + 哈希打分 |
| **Proxy 总时延** | **~850ms** | 50 + max(200, 100) + 500 + 网络开销 |

### 6.2 优化方向

1. **超时配置**：根据实际生产环境的 P99 时延调整各下游超时，避免过长等待
2. **连接池**：BRPC `pooled` 模式减少连接建立开销
3. **请求合并**：如果 FeatureService 支持批量，可将一段时间内的请求合并
4. **异步非阻塞**：对于极高并发场景，可考虑完全异步的回调模式代替 `future.get()` 阻塞

## 7. 演进规划

| 版本 | 特性 |
|------|------|
| V1.0 | 基础编排功能：Feature → {Recall ∥ Precalc} → Rank 同步调用 |
| V1.1 | 部分失败降级：Recall 失败时使用缓存结果 |
| V1.2 | 请求级别超时控制：支持动态调整超时 |
| V2.0 | 异步全非阻塞：完全基于 BRPC 回调的异步编排 |
