# LingQuickRec API 接口文档

本文档详细说明了 LingQuickRec 系统中各个服务的接口定义和使用方法。

## 目录

1. [Proxy 服务（网关）](#proxy-服务网关)
2. [FeatureService（特征服务）](#featureservice-特征服务)
3. [RecallService（召回服务）](#recallservice-召回服务)
4. [PrecalcService（前置计算服务）](#precalcservice-前置计算服务)
5. [RankService（精排服务）](#rankservice-精排服务)

---

## Proxy 服务（网关）

**服务名称**: `Proxy`  
**包名**: `proxy`  
**端口**: 未指定（网关入口）  
**Proto 文件**: `proxy.proto`  
**实现状态**: 规划中

### 接口定义

```protobuf
service Proxy {
    rpc Recommend(RecommendRequest) returns (RecommendResponse);
}
```

### 消息定义

#### RecommendRequest

推荐请求

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `user_id` | `uint64` | 1 | 用户 ID |
| `payload` | `string` | 2 | 附加数据 |

#### RecommendResponse

推荐响应

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `candidates` | `repeated uint64` | 1 | 候选商品 ID 列表 |

---

## FeatureService（特征服务）

**服务名称**: `FeatureService`  
**包名**: `feature`  
**端口**: 8003  
**Proto 文件**: `feature.proto`  
**实现状态**: 规划中

### 接口定义

```protobuf
service FeatureService {
    rpc GetUserFeatures(UserFeatureRequest) returns (UserFeatureResponse);
    rpc GetSKUFeatures(SKUFeatureRequest) returns (SKUFeatureResponse);
}
```

### 消息定义

#### FeatureType

特征类型枚举

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | `KuaiRand` | 快推特征 |
| 1 | `UNKNOWN` | 未知类型 |

#### KRUserFeatureRequest

快推用户特征请求

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `user_id` | `uint64` | 1 | 用户 ID |
| `req_data` | `string` | 2 | 请求数据 |

#### KRUserLog

快推用户日志

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `vec` | `repeated uint32` | 1 | 特征向量 |

#### KRUserFeatureResponse

快推用户特征响应

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `user_logs` | `repeated KRUserLog` | 1 | 用户日志列表 |
| `other` | `string` | 2 | 其他数据 |

#### UserFeatureRequest

用户特征请求

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `feature_type` | `FeatureType` | 1 | 特征类型 |
| `kr_feat_req` | `KRUserFeatureRequest` | 2 | 快推特征请求（可选） |

#### UserFeatureResponse

用户特征响应

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `feature_type` | `FeatureType` | 1 | 特征类型 |
| `kr_feat_rsp` | `KRUserFeatureResponse` | 2 | 快推特征响应（可选） |

#### SKUFeatureRequest

SKU 特征请求

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `feature_type` | `FeatureType` | 1 | 特征类型 |
| `sku_ids` | `repeated uint64` | 2 | SKU ID 列表 |

#### KRSKUFeature

快推 SKU 特征

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `sku_id` | `uint64` | 1 | SKU ID |
| `feat` | `string` | 2 | 特征数据 |

#### SKUFeatureResponse

SKU 特征响应

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `feature_type` | `FeatureType` | 1 | 特征类型 |
| `kr_sku_feats` | `repeated KRSKUFeature` | 2 | 快推 SKU 特征列表 |

### 使用示例

```cpp
// 获取用户特征
feature::UserFeatureRequest request;
request.set_feature_type(feature::KuaiRand);

feature::KRUserFeatureRequest* kr_req = request.mutable_kr_feat_req();
kr_req->set_user_id(12345);
kr_req->set_req_data("test_data");

feature::UserFeatureResponse response;
brpc::Controller cntl;

feature::FeatureService_Stub stub(&channel);
stub.GetUserFeatures(&cntl, &request, &response, nullptr);

if (cntl.Failed()) {
    LOG(ERROR) << "RPC failed: " << cntl.ErrorText();
    return;
}

// 处理响应
const auto& kr_rsp = response.kr_feat_rsp();
for (int i = 0; i < kr_rsp.user_logs_size(); ++i) {
    const auto& log = kr_rsp.user_logs(i);
    LOG(INFO) << "User log " << i << " vec size: " << log.vec_size();
}
```

---

## RecallService（召回服务）

**服务名称**: `RecallService`  
**包名**: `recall`  
**端口**: 8001  
**Proto 文件**: `recall.proto`  
**实现状态**: ✅ 已完成

### 接口定义

```protobuf
service RecallService {
    rpc Recall(RecallRequest) returns (RecallResponse);
}
```

### 消息定义

#### KRUserLog

快推用户日志（与特征服务共享）

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `vec` | `repeated uint32` | 1 | 特征向量 |

#### RecallRequest

召回服务请求

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `user_id` | `uint64` | 1 | 用户 ID |
| `user_logs` | `repeated KRUserLog` | 2 | 用户日志列表 |
| `other` | `string` | 3 | 其他数据 |

#### RecallResponse

召回服务响应

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `sku_ids` | `repeated uint64` | 1 | 召回的 SKU ID 列表 |

### 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8001 | 服务器监听端口 |
| `--sku_count` | 1000 | 返回的 SKU ID 数量 |
| `--thread_pool_size` | 128 | 线程池大小 |
| `--vllm_timeout_ms` | 5000 | vLLM 请求超时时间 |

### 使用示例

```cpp
// 客户端调用示例
recall::RecallRequest request;
request.set_user_id(12345);
request.set_other("test_request");

// 添加用户日志
for (int i = 0; i < 3; ++i) {
    recall::KRUserLog* log = request.add_user_logs();
    for (int j = 0; j < 5; ++j) {
        log->add_vec(i * 10 + j);
    }
}

recall::RecallResponse response;
brpc::Controller cntl;

recall::RecallService_Stub stub(&channel);
stub.Recall(&cntl, &request, &response, nullptr);

if (cntl.Failed()) {
    LOG(ERROR) << "RPC failed: " << cntl.ErrorText();
    return;
}

// 处理响应 - 获取召回的 SKU ID 列表
LOG(INFO) << "Recalled " << response.sku_ids_size() << " SKU IDs";
for (int i = 0; i < response.sku_ids_size(); ++i) {
    LOG(INFO) << "SKU " << i << ": " << response.sku_ids(i);
}
```

### 服务端配置

```bash
# 启动 Recall 服务
./recall_server \
    --server_port=8001 \
    --vllm_base_url="http://127.0.0.1:8000" \
    --vllm_endpoint="/v1/chat/completions" \
    --model_name="/workspace/share/Qwen3-0.6B" \
    --sku_count=1000 \
    --thread_pool_size=128 \
    --vllm_timeout_ms=5000 \
    --logtostderr
```

---

## PrecalcService（前置计算服务）

**服务名称**: `PrecalcService`  
**包名**: `precalc`  
**端口**: 8004  
**Proto 文件**: `precalc.proto`  
**实现状态**: ✅ 已完成

### 接口定义

```protobuf
service PrecalcService {
    rpc Precalculate(PrecalcRequest) returns (PrecalcResponse);
}
```

### 消息定义

#### PrecalcRequest

前置计算服务请求

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `user_feat` | `string` | 1 | 用户特征数据（100KB） |

#### PrecalcResponse

前置计算服务响应

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `user_feat_key` | `string` | 1 | 用户特征 key（格式：user_id + 时间戳） |
| `payload` | `string` | 2 | 负载数据（用于模拟 100KB 响应） |

### 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8004 | 服务器监听端口 |
| `--kvworker_host` | "127.0.0.1" | 元戎 KVWorker 主机地址 |
| `--kvworker_port` | 8002 | 元戎 KVWorker 端口 |
| `--precalc_result_size_mb` | 8.5 | 前置计算结果大小（MB），默认 8.5MB |
| `--ttl_seconds` | 5 | TTL 时间（秒） |
| `--response_total_size_kb` | 100 | 响应总大小（key+payload），默认 100KB |

### 特性

- **Key 生成**: `user_id + "_" + timestamp`（微秒级时间戳）
- **前置计算结果**: 8.5MB 随机 tensor 数据，使用元戎 Create+Set 接口写入 KVWorker
- **TTL**: 5 秒后数据自动删除
- **通信协议**: 
  - 与 Proxy 服务：BRPC
  - 与 KVWorker：元戎（openYuanrong）

### 使用示例

```cpp
// 客户端调用示例
precalc::PrecalcRequest request;
request.set_user_feat("user_feature_data...");

precalc::PrecalcResponse response;
brpc::Controller cntl;

precalc::PrecalcService_Stub stub(&channel);
stub.Precalculate(&cntl, &request, &response, nullptr);

if (cntl.Failed()) {
    LOG(ERROR) << "RPC failed: " << cntl.ErrorText();
    return;
}

// 处理响应
LOG(INFO) << "Precalc result:";
LOG(INFO) << "  Key: " << response.user_feat_key();
LOG(INFO) << "  Payload size: " << response.payload().size() << " bytes";
```

### 服务端配置

```bash
# 启动 Precalc 服务
./precalc_server \
    --server_port=8004 \
    --result_size_mb=8.5 \
    --ttl_seconds=5 \
    --logtostderr
```

---

## RankService（精排服务）

**服务名称**: `RankService`  
**包名**: `rank`  
**端口**: 8005  
**Proto 文件**: `rank.proto`  
**实现状态**: 规划中

### 接口定义

```protobuf
service RankService {
    rpc Rank(RankRequest) returns (RankResponse);
}
```

### 消息定义

#### RankRequest

精排服务请求

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `user_feat_key` | `string` | 1 | 用户特征 key（从 PrecalcService 获取） |
| `skus` | `string` | 2 | SKU 数据（100KB） |
| `payload` | `string` | 3 | 附加数据（100KB） |

#### RankResponse

精排服务响应

| 字段 | 类型 | 编号 | 说明 |
|------|------|------|------|
| `candidates` | `repeated uint64` | 1 | 排序后的候选商品 ID 列表 |

### 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8005 | 服务器监听端口 |

---

## 附录

### Proto 文件位置

所有 Proto 文件位于 `proto/` 目录：
- `proxy.proto` - Proxy 服务
- `feature.proto` - FeatureService
- `recall.proto` - RecallService
- `precalc.proto` - PrecalcService
- `rank.proto` - RankService

### 通用错误码

| 错误码 | 说明 |
|--------|------|
| 0 | 成功 |
| 400 | 请求参数错误 |
| 500 | 服务端内部错误 |
| 502 | 依赖服务错误 |
| 503 | 服务繁忙 |
| 504 | 请求超时 |

### 最佳实践

1. **超时设置**: 建议为所有 RPC 调用设置合理的超时时间
2. **重试机制**: 对于临时性错误，可以实现重试机制
3. **错误处理**: 始终检查 `cntl.Failed()` 并处理错误
4. **日志记录**: 记录关键请求和响应信息，便于调试和监控
