# Proxy 网关服务详细设计方案

## 1. 设计目标

### 1.1 核心目标

- **统一网关入口**：作为系统唯一对外暴露的 HTTP 端点，接收外部推荐请求
- **请求编排**：按业务逻辑依次调用 Feature → {Recall ∥ Precalc} → Rank 四个下游服务
- **动态服务发现**：通过 Discovery Server 获取下游实例，不配置静态地址
- **容错机制**：支持实例级熔断（连续 3 次失败触发 10s 冷却）、自动重试、round-robin 负载均衡
- **可观测性**：记录各阶段时延，支持全链路 trace_id 追踪

### 1.2 设计原则

- **零静态配置**：下游服务地址通过 Discovery 动态获取，无需配置静态 IP
- **服务名驱动**：通过服务名（如 `feature_service`）而非地址进行服务调用
- **容错优先**：单实例失败自动重试其他实例，连续失败触发熔断保护
- **与现有模式一致**：日志风格、错误码体系、线程池使用与项目其他服务保持一致

## 2. 系统架构

### 2.1 整体架构

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           Proxy 服务进程                                   │
│                                                                          │
│  ┌──────────────────────┐    ┌────────────────────────────────────┐      │
│  │   BRPC HTTP Server   │    │        全局线程池                    │      │
│  │   (0.0.0.0:8080)     │    │  (common::get_global_thread_pool)  │      │
│  │                      │    │                                    │      │
│  │  POST /Proxy/Recommend──┼──▶ submit(process_recommend_request) │      │
│  └──────────────────────┘    └──────────┬─────────────────────────┘      │
│                                         │                                │
│  ┌──────────────────────────────────────▼───────────────────────────────┐│
│  │                    Request Processing Pipeline                        ││
│  │                                                                        ││
│  │  ┌──────────┐    ┌──────────────────┐    ┌───────────┐                ││
│  │  │ Stage 1  │───▶│  Stage 2         │───▶│ Stage 3   │                ││
│  │  │ Feature  │    │ Recall ∥ Precalc │    │ Rank      │                ││
│  │  └──────────┘    └──────────────────┘    └───────────┘                ││
│  └────────────────────────────────────────────────────────────────────────┘│
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │                     ServiceDiscovery                                  ││
│  │  ┌──────────────────────────────────────────────────────────────────┐ ││
│  │  │  Instance Cache (后台线程定时刷新)                                  │ ││
│  │  │  "feature_service" → [{host, port, instance_id, state}]          │ ││
│  │  │  "recall_service"  → [{...}]                                     │ ││
│  │  │  "precalc_service" → [{...}]                                     │ ││
│  │  │  "rank_service"    → [{...}]                                     │ ││
│  │  └──────────────────────────────────────────────────────────────────┘ ││
│  │                                                                        ││
│  │  ┌──────────────────────────────────────────────────────────────────┐ ││
│  │  │  Circuit Breaker State                                            │ ││
│  │  │  instance_id → {consecutive_failures, cooldown_until}            │ ││
│  │  │  MAX_CONSECUTIVE_FAILURES = 3 → cooldown 10s                     │ ││
│  │  └──────────────────────────────────────────────────────────────────┘ ││
│  │                                                                        ││
│  │  ┌────────────────────────────────────────────────────────────────┐   ││
│  │  │  Round-Robin Index (per service)                                 │   ││
│  │  │  rr_index_[service_name] → next instance position               │   ││
│  │  └────────────────────────────────────────────────────────────────┘   ││
│  └──────────────────────────────────────────────────────────────────────┘ │
│                                          │                                │
│                                          │ GetInstance()                  │
│                                          │ ReportFailure() / ReportSuccess()│
│                                          ▼                                │
│  ┌──────────────────────────────────────────────────────────────────────┐ │
│  │  Discovery Server (:8100)                                             │ │
│  │  RPC: Discover(service_name) → UP instances                          │ │
│  └──────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
```

### 2.2 目录结构

```
services/proxy/
├── CMakeLists.txt                     # 构建配置
├── Dockerfile                         # 容器镜像定义
├── README.md                          # 服务概述和业务流程
├── DESIGN.md                          # 本文档 - 详细设计
├── server/
│   ├── include/
│   │   ├── proxy_server.h             # ProxyServiceImpl 类声明
│   │   └── service_discovery.h        # ServiceDiscovery 类声明
│   └── src/
│       ├── main.cpp                   # 服务入口：gflags 解析、日志初始化、Server 启动
│       ├── proxy_server.cpp           # 核心编排逻辑：call_with_retry 模板
│       └── service_discovery.cpp      # 服务发现实现：刷新、熔断、round-robin
├── client/
│   └── proxy_test_client.cpp          # 手动测试客户端
└── tests/
    └── integration_test.cpp           # 单进程集成测试（6 个 Mock Server）
```

### 2.3 核心组件

| 组件 | 职责 |
|------|------|
| `ProxyServiceImpl` | BRPC 服务实现类，实现 `proxy::Proxy` 接口的 `Recommend` 方法 |
| `ServiceDiscovery` | 服务发现客户端：定时刷新实例列表、round-robin 选择、熔断状态管理 |
| `process_recommend_request()` | 核心编排逻辑：依次/并行调用 4 个下游服务 |
| `call_with_retry()` | 模板方法：获取实例 → 创建 Channel → 调用 RPC → 失败重试/成功上报 |
| `call_feature_service()` | 封装 FeatureService 的 RPC 调用 |
| `call_recall_service()` | 封装 RecallService 的 RPC 调用 |
| `call_precalc_service()` | 封装 PrecalcService 的 RPC 调用 |
| `call_rank_service()` | 封装 RankServiceMaster 的 RPC 调用 |

## 3. 详细设计

### 3.1 服务器层

Proxy 使用 **BRPC 内建 HTTP 支持** 对外提供服务。BRPC 的 `brpc::Server` 原生支持 HTTP 协议——当客户端通过 HTTP POST 请求 `/Proxy/Recommend` 时，BRPC 自动将 HTTP Body（JSON）反序列化为 `RecommendRequest` 并路由到 `Recommend` 方法。

```cpp
// main.cpp 关键逻辑
int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    // 初始化日志系统
    common::logger::AddConsoleSink();
    common::logger::SetTraceIdGetter([]() { return proxy::get_current_trace_id(); });

    // 自动计算线程池大小
    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_global_thread_pool_size == 128) {
        FLAGS_global_thread_pool_size = std::min(128, std::max(4, cpu_cores * 2));
    }

    // 创建服务实例（内部初始化 ServiceDiscovery）
    proxy::ProxyServiceImpl service_impl;

    // 创建 BRPC 服务器
    brpc::Server server;
    server.AddService(&service_impl, brpc::SERVER_DOESNT_OWN_SERVICE);

    // 启动监听
    server.Start(FLAGS_server_port, nullptr);
    server.RunUntilAskedToQuit();
}
```

### 3.2 服务发现机制

#### 3.2.1 ServiceDiscovery 类

`ServiceDiscovery` 类负责：
- 维护各服务的实例缓存（定时从 Discovery Server 刷新）
- 提供 `GetInstance()` 接口按 round-robin 选择可用实例
- 管理 `ReportSuccess()` / `ReportFailure()` 上报实例状态
- 实现熔断器逻辑

```cpp
class ServiceDiscovery {
public:
    ServiceDiscovery(const std::string& discovery_addr, int refresh_interval_ms);
    ~ServiceDiscovery();

    // 获取下一个可用实例（跳过 cooldown 中的实例）
    bool GetInstance(const std::string& service_name,
                     std::string& host, int& port, std::string& instance_id);

    // 成功调用后重置熔断状态
    void ReportSuccess(const std::string& instance_id);

    // 失败后累计失败次数，触发熔断
    void ReportFailure(const std::string& instance_id);

private:
    void refresh_loop();      // 后台刷新线程
    void refresh_all();       // 刷新所有已知服务的实例列表

    // 实例状态：失败次数 + cooldown 截止时间
    struct InstanceState {
        int consecutive_failures = 0;
        std::chrono::steady_clock::time_point cooldown_until;
        bool in_cooldown() const;
    };

    std::unique_ptr<discovery::DiscoveryService_Stub> stub_;
    std::unordered_map<std::string, std::vector<discovery::ServiceInstance>> cache_;
    std::unordered_map<std::string, size_t> rr_index_;            // round-robin 索引
    std::unordered_map<std::string, InstanceState> instance_states_; // 熔断状态
    std::thread refresh_thread_;
    std::atomic<bool> running_{true};
};
```

#### 3.2.2 刷新机制

后台线程每隔 `refresh_interval_ms` 调用 Discovery Server 的 `Discover()` RPC：

```cpp
void ServiceDiscovery::refresh_all() {
    std::vector<std::string> services = {
        FLAGS_feature_service_name,
        FLAGS_recall_service_name,
        FLAGS_precalc_service_name,
        FLAGS_rank_service_name
    };

    for (const auto& svc : services) {
        discovery::DiscoverRequest req;
        req.set_service_name(svc);
        req.set_include_down(false);  // 只获取 UP 状态实例

        discovery::DiscoverResponse rsp;
        brpc::Controller cntl;
        cntl.set_timeout_ms(2000);

        stub_->Discover(&cntl, &req, &rsp, nullptr);

        if (!cntl.Failed()) {
            std::lock_guard<std::mutex> lock(mutex_);
            cache_[svc] = {rsp.instances().begin(), rsp.instances().end()};
        }
    }
}
```

#### 3.2.3 熔断器逻辑

```
                     调用成功
    ┌──────────────────────────────────────┐
    │                                      ▼
    │   ┌─────────────────────────────────────┐
    │   │ consecutive_failures = 0            │
    │   │ (清除 InstanceState)                │
    │   └─────────────────────────────────────┘
    │
    │                     调用失败
    │   ┌─────────────────────────────────────┐
    │   │ consecutive_failures++              │
    │   └─────────────────────────────────────┘
    │                 │
    │                 │ consecutive_failures >= 3
    │                 ▼
    │   ┌─────────────────────────────────────┐
    │   │ cooldown_until = now + 10s          │
    │   │ GetInstance() 跳过该实例             │
    │   └─────────────────────────────────────┘
    │                 │
    │                 │ 10s 后 cooldown 过期
    │                 ▼
    │   ┌─────────────────────────────────────┐
    │   │ 该实例重新参与 round-robin           │
    │   │ 成功 → 清除状态                      │
    │   │ 失败 → 再次进入 cooldown             │
    │   └─────────────────────────────────────┘
```

### 3.3 call_with_retry 模板方法

所有下游调用统一通过 `call_with_retry()` 方法，封装了实例获取、Channel 创建、RPC 调用、失败重试、状态上报的完整流程：

```cpp
Status ProxyServiceImpl::call_with_retry(
    const std::string& service_name,
    int timeout_ms,
    uint32_t error_specific_code,
    const std::function<Status(brpc::Channel&, brpc::Controller&)>& rpc_impl) {

    std::vector<std::string> tried;

    for (int attempt = 0; attempt <= FLAGS_downstream_max_retries; ++attempt) {
        std::string host, instance_id;
        int port;

        // 1. 获取下一个可用实例（跳过 cooldown 实例）
        if (!discovery_->GetInstance(service_name, host, port, instance_id)) {
            if (attempt == 0) {
                return Status::Error(MODULE::GATEWAY, TYPE::SERVICE_ERROR,
                                     error_specific_code,
                                     service_name + ": no available instances");
            }
            break;
        }

        std::string addr = host + ":" + std::to_string(port);
        tried.push_back(addr);

        // 2. 动态创建 Channel（不复用，每次调用新建）
        brpc::Channel channel;
        brpc::ChannelOptions opts;
        opts.timeout_ms = timeout_ms;
        opts.connection_type = "pooled";
        opts.max_retry = 0;  // 我们自己控制重试

        if (channel.Init(addr.c_str(), &opts) != 0) {
            discovery_->ReportFailure(instance_id);
            continue;
        }

        // 3. 发起 RPC 调用
        brpc::Controller cntl;
        cntl.set_timeout_ms(timeout_ms);
        cntl.set_log_id(trace_log_id);  // 传递 trace_id

        auto st = rpc_impl(channel, cntl);

        // 4. 失败处理
        if (cntl.Failed()) {
            discovery_->ReportFailure(instance_id);
            if (attempt < FLAGS_downstream_max_retries) continue;
            return Status::Error(MODULE::GATEWAY, TYPE::SERVICE_ERROR,
                                 error_specific_code, cntl.ErrorText());
        }

        // 5. 成功上报
        discovery_->ReportSuccess(instance_id);
        return Status::OK();
    }

    return Status::Error(MODULE::GATEWAY, TYPE::SERVICE_ERROR,
                         error_specific_code,
                         service_name + ": all instances failed");
}
```

### 3.4 请求处理管线

```
Recommend(request, response, done)
  │
  ├─▶ 生成 trace_id (32字符 hex)
  │    tls_trace_id = timestamp_16hex + random_16hex
  │
  ├─▶ process_recommend_request(request, response)
  │    │
  │    │  [Stage 1] 获取特征（同步）
  │    ├─▶ call_feature_service(request, user_feat)
  │    │    │
  │    │    │  失败 → 返回 error_code=0x01030001，终止
  │    │    │
  │    │    ▼
  │    │  [Stage 2] 召回 + 预计算（并行）
  │    ├─▶ pool.submit(call_recall_service, user_feat)  → future1
  │    ├─▶ pool.submit(call_precalc_service, user_feat) → future2
  │    │    │
  │    │    │  future1.get() + future2.get()
  │    │    │
  │    │    │  任一失败 → 返回对应 error_code，终止
  │    │    │
  │    │    ▼
  │    │  [Stage 3] 精排（同步）
  │    └─▶ call_rank_service(recall_rsp, precalc_rsp, response)
  │         │
  │         │  失败 → 返回 error_code=0x01030004
  │         │
  │         ▼
  │       成功 → response.candidates 填充结果
  │
  └─▶ ClosureGuard 自动调用 done->Run()
```

### 3.5 Stage 1: 特征获取

**调用方式**：同步阻塞。必须在获取用户特征后才能进行后续的召回和预计算。

```cpp
Status ProxyServiceImpl::call_feature_service(
    const RecommendRequest* request,
    feature::UserFeatureResponse* response) {

    feature::UserFeatureRequest feat_req;
    feat_req.set_feature_type(feature::KuaiRand);
    feat_req.mutable_kr_feat_req()->set_user_id(request->user_id());
    feat_req.mutable_kr_feat_req()->set_req_data(request->payload());

    auto rpc_impl = [&](brpc::Channel& ch, brpc::Controller& cntl) {
        feature::FeatureService_Stub stub(&ch);
        stub.GetUserFeatures(&cntl, &feat_req, response, nullptr);
        return Status::OK();
    };

    return call_with_retry(FLAGS_feature_service_name,
                           FLAGS_feature_timeout_ms, 0x0001, rpc_impl);
}
```

### 3.6 Stage 2: 并行召回 & 预计算

**调用方式**：使用全局线程池 `pool.submit()` 发起两个并发任务，使用 `future.get()` 同步等待。

```cpp
// 并行发起
auto& pool = common::get_global_thread_pool();

auto recall_future = pool.submit([this, user_id, &user_feat]() {
    tls_trace_id = current_trace_id;  // 传递 trace_id 到子线程
    recall::RecallResponse rsp;
    auto st = call_recall_service(user_id, user_feat, &rsp);
    return std::make_pair(st, std::move(rsp));
});

auto precalc_future = pool.submit([this, user_id, &user_feat]() {
    tls_trace_id = current_trace_id;
    precalc::PrecalcResponse rsp;
    auto st = call_precalc_service(user_id, user_feat, &rsp);
    return std::make_pair(st, std::move(rsp));
});

// 等待两者完成
auto [recall_st, recall_rsp] = recall_future.get();
auto [precalc_st, precalc_rsp] = precalc_future.get();

// 任一失败则终止
if (!recall_st.IsOk() || !precalc_st.IsOk()) {
    return !recall_st.IsOk() ? recall_st : precalc_st;
}
```

**关键说明**：RankServiceMaster 同时依赖 Recall 的 `sku_ids` 和 Precalc 的 `user_feat_key`，因此两者中任一失败都无法继续精排阶段。

### 3.7 Stage 3: 精排

**调用方式**：同步阻塞。将 Stage 2 的结果拼接为 `RankMasterRequest` 发送给 RankServiceMaster。

```cpp
Status ProxyServiceImpl::call_rank_service(
    const recall::RecallResponse& recall_rsp,
    const precalc::PrecalcResponse& precalc_rsp,
    RecommendResponse* response) {

    rank::RankMasterRequest rank_req;
    rank_req.set_user_feat_key(precalc_rsp.user_feat_key());

    // SKU ID 格式化为 6 位定长字符串
    std::ostringstream skus_oss;
    for (int i = 0; i < recall_rsp.sku_ids_size(); ++i) {
        skus_oss << std::setw(6) << std::setfill('0') << recall_rsp.sku_ids(i);
    }
    rank_req.set_skus(skus_oss.str());
    rank_req.set_payload(precalc_rsp.payload());

    auto rpc_impl = [&](brpc::Channel& ch, brpc::Controller& cntl) {
        rank::RankMasterService_Stub stub(&ch);
        rank::RankMasterResponse rank_rsp;
        stub.Rank(&cntl, &rank_req, &rank_rsp, nullptr);

        if (!cntl.Failed()) {
            for (int i = 0; i < rank_rsp.candidates_size(); ++i) {
                response->add_candidates(rank_rsp.candidates(i));
            }
        }
        return Status::OK();
    };

    return call_with_retry(FLAGS_rank_service_name,
                           FLAGS_rank_timeout_ms, 0x0004, rpc_impl);
}
```

### 3.8 错误处理

| 场景 | error_code | 行为 |
|------|-----------|------|
| Feature 调用失败/超时 | 0x01030001 | 终止请求，返回错误 |
| Recall 调用失败/超时 | 0x01030002 | 终止请求，返回错误 |
| Precalc 调用失败/超时 | 0x01030003 | 终止请求，返回错误 |
| Rank 调用失败/超时 | 0x01030004 | 终止请求，无候选结果 |
| 服务无可用实例 | 0x0103XXXX | 返回 "no available instances" |
| 全部实例均失败 | 0x0103XXXX | 返回 "all instances failed" |
| 全部成功 | 0 | 正常返回 candidates |

**错误码格式**：`0xMMTTCCCC`

| 字节 | 含义 | 值 |
|------|------|---|
| MM | 模块 (GATEWAY) | 0x01 |
| TT | 类型 (SERVICE_ERROR) | 0x03 |
| CCCC | 具体错误码 | 0x0001~0x0004 |

### 3.9 trace_id 机制

每个请求在入口生成 trace_id：**32 字符 hex** = 前 16 字符微秒时间戳 + 后 16 字符随机数。

```cpp
std::string generate_trace_id() {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();

    static thread_local std::mt19937_64 rng(std::random_device{}());
    uint64_t rand_val = rng();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << us
        << std::setw(16) << rand_val;
    return oss.str();
}
```

**传递方式**：
- 使用 thread-local 变量 `tls_trace_id` 存储当前请求的 trace_id
- 通过 `cntl.set_log_id()` 传递到下游 RPC
- 线程池任务通过 lambda 捕获传递 trace_id
- 日志系统通过 `SetTraceIdGetter()` 回调获取 trace_id

### 3.10 并发模型

```
BRPC I/O 程
    │
    │  Recommend() 被调用
    │
    ▼
common::get_global_thread_pool().submit(process_recommend_request)
    │
    │  线程池线程 T1 执行 process_recommend_request()
    │
    ├── call_feature_service()         ← 同步阻塞 (T1)
    │   └── call_with_retry() → Discovery.GetInstance → Channel.Init → RPC
    │
    ├── pool.submit(call_recall)       ← 新线程 T2
    ├── pool.submit(call_precalc)      ← 新线程 T3
    │   ├── T1 等待 T2、T3 完成
    │
    └── call_rank_service()            ← 同步阻塞 (T1)
        └── call_with_retry() → Discovery.GetInstance → Channel.Init → RPC
```

**线程安全说明**：
- 每个请求在独立的线程池线程中处理，请求间天然隔离
- `ServiceDiscovery` 使用 mutex 保护实例缓存和状态
- `std::async` 任务通过 lambda 捕获传递 trace_id
- Channel 每次调用动态创建，不复用

### 3.11 可观测性

#### 阶段时延统计

使用 `std::chrono::steady_clock` 记录各阶段耗时：

```cpp
auto t0 = std::chrono::steady_clock::now();

// Stage 1: Feature
feature::UserFeatureResponse user_feat;
auto feat_st = call_feature_service(request, &user_feat);
auto t1 = std::chrono::steady_clock::now();

// Stage 2: Recall + Precalc
auto [recall_st, recall_rsp] = recall_future.get();
auto [precalc_st, precalc_rsp] = precalc_future.get();
auto t2 = std::chrono::steady_clock::now();

// Stage 3: Rank
auto rank_st = call_rank_service(recall_rsp, precalc_rsp, response);
auto t3 = std::chrono::steady_clock::now();

if (FLAGS_enable_timing_stats) {
    LOG_INFO << "[Proxy Timing] "
              << " feature=" << (t1-t0)/1000.0 << "ms"
              << " recall+precalc=" << (t2-t1)/1000.0 << "ms"
              << " rank=" << (t3-t2)/1000.0 << "ms"
              << " total=" << (t3-t0)/1000.0 << "ms";
}
```

## 4. 配置参数

| 参数名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| **服务发现** | | | |
| `--discovery_addr` | string | "127.0.0.1:8100" | Discovery Server 地址 |
| `--discovery_refresh_interval_ms` | int32 | 5000 | 实例缓存刷新间隔 (ms) |
| `--downstream_max_retries` | int32 | 2 | 每个下游最大重试次数 |
| **下游服务名** | | | |
| `--feature_service_name` | string | "feature_service" | Feature 服务注册名 |
| `--recall_service_name` | string | "recall_service" | Recall 服务注册名 |
| `--precalc_service_name` | string | "precalc_service" | Precalc 服务注册名 |
| `--rank_service_name` | string | "rank_service" | Rank 服务注册名 |
| **超时** | | | |
| `--feature_timeout_ms` | int32 | 3000 | Feature 调用超时 (ms) |
| `--recall_timeout_ms` | int32 | 5000 | Recall 调用超时 (ms) |
| `--precalc_timeout_ms` | int32 | 5000 | Precalc 调用超时 (ms) |
| `--rank_timeout_ms` | int32 | 10000 | Rank 调用超时 (ms) |
| **其他** | | | |
| `--server_port` | int32 | 8080 | Proxy HTTP 服务监听端口 |
| `--enable_timing_stats` | bool | true | 是否打印阶段时延统计 |
| `--global_thread_pool_size` | int32 | 128 (auto) | 全局线程池大小，默认根据 CPU 核数自动计算 |

## 5. API 契约

### 5.1 Proto 定义

```protobuf
// proto/proxy.proto
service Proxy {
    rpc Recommend(RecommendRequest) returns (RecommendResponse);
}

message RecommendRequest {
    uint64 user_id = 1;
    string payload = 2;  // 附加数据 / trace_id 透传
}

message RecommendResponse {
    repeated uint64 candidates = 1;  // 排序后的候选 SKU ID
    int32 error_code = 2;            // 0 = 成功，非 0 = 错误码
    string error_message = 3;        // 错误描述
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

## 6. 服务集成

Proxy 调用各下游服务时的消息映射：

| Proxy 输出 | 下游输入 | 说明 |
|-----------|---------|------|
| `request.user_id` → | FeatureService `UserFeatureRequest.kr_feat_req.user_id` | 用户 ID |
| `request.payload` → | FeatureService `UserFeatureRequest.kr_feat_req.req_data` | 附加数据 |
| Feature `kr_feat_rsp.other` → | RecallService `RecallRequest.other` | 用户特征字符串 |
| Feature `kr_feat_rsp.user_logs` → | RecallService `RecallRequest.user_logs` | 用户行为日志 |
| Feature `kr_feat_rsp.other` → | PrecalcService `PrecalcRequest.user_feat` | 用户特征 |
| Recall `sku_ids` → | RankService `RankMasterRequest.skus` | 候选 SKU ID (6 位定长格式) |
| Precalc `user_feat_key` → | RankService `RankMasterRequest.user_feat_key` | 预计算结果索引 |
| Precalc `payload` → | RankService `RankMasterRequest.payload` | 附加数据 |

## 7. 集成测试

### 7.1 测试架构

单进程测试，启动 6 个 Mock Server 模拟完整链路：

```
┌──────────────────────────────────────────────────────────┐
│                Single Process (test binary)              │
│                                                          │
│  MockDiscoveryService  (:18100)                          │
│  └───────────────────────────────────────────────────────│
│  Register("feature_service", 127.0.0.1:18001)            │
│  Register("recall_service",  127.0.0.1:18002)            │
│  Register("precalc_service", 127.0.0.1:18003)            │
│  Register("rank_service",    127.0.0.1:18004)            │
│                                                          │
│  MockFeature(:18001) MockRecall(:18002)                  │
│  MockPrecalc(:18003) MockRank(:18004)                    │
│                                                          │
│  ProxyServiceImpl (:18000)                               │
│  └───────────────────────────────────────────────────────│
│  ServiceDiscovery → MockDiscovery                        │
│  call_feature → MockFeature                              │
│  call_recall → MockRecall                                │
│  call_precalc → MockPrecalc                              │
│  call_rank → MockRank                                    │
└──────────────────────────────────────────────────────────┘
```

### 7.2 测试场景

| 场景 | 验证内容 |
|------|----------|
| Happy path | 3 个 candidates 按正确顺序返回，error_code=0 |
| Feature fails | error_code=0x01030001 |
| Recall fails | error_code=0x01030002 |
| Precalc fails | error_code=0x01030003 |
| Rank fails | error_code=0x01030004 |

## 8. 性能分析

### 8.1 时延预算

假设各下游 P99 时延：

| 阶段 | 估算 P99 时延 | 说明 |
|------|--------------|------|
| Feature | ~50ms | 模拟实现（无真实数据源） |
| Recall | ~200ms | vLLM 推理 + HTTP 通信 |
| Precalc | ~100ms | KVWorker 写入 + 数据生成 |
| Rank | ~500ms | 子图并行 + KVWorker 读取 |
| **Proxy 总时延** | **~650ms** | 50 + max(200, 100) + 500 |

### 8.2 优化方向

1. **实例缓存刷新频率**：根据实际服务变化频率调整 `discovery_refresh_interval_ms`
2. **熔断阈值调整**：根据网络稳定性调整 `MAX_CONSECUTIVE_FAILURES` 和 `COOLDOWN_SECONDS`
3. **并行度优化**：Stage 2 已并行，后续可考虑 Feature 与其他阶段的流水线重叠
4. **Channel 复用**：当前每次调用新建 Channel，高频场景可考虑 Channel 池化

## 9. 演进规划

| 版本 | 特性 |
|------|------|
| **V1.0** | 已完成：动态服务发现、熔断器、重试逻辑、round-robin、trace_id |
| **V1.1** | Watch/Notify 推送：Discovery Server 主动推送实例变更，降低发现延迟 |
| **V1.2** | Channel 池化：复用 Channel 减少连接建立开销 |
| **V2.0** | 异步全非阻塞：完全基于 BRPC 回调的异步编排 |