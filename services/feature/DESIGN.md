# 特征服务设计方案

## 1. 设计目标

FeatureService 是推荐系统的特征服务层，负责提供用户特征和 SKU 特征数据，供上游服务（如 RecallService）在召回和排序阶段使用。

**核心需求**：

- 接收用户特征请求，返回用户行为日志和附加信息
- 接收 SKU 特征请求，返回 SKU 对应的特征数据
- 当前为模拟实现，通过随机数生成测试特征数据，用于端到端流水线验证
- 支持 KuaiRand 特征类型

## 2. 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                      FeatureService (:8003)                      │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    BRPC RPC Handler                        │  │
│  │  GetUserFeatures(UserFeatureRequest) → UserFeatureResponse│  │
│  │  GetSKUFeatures(SKUFeatureRequest) → SKUFeatureResponse   │  │
│  └──────────────────────────┬────────────────────────────────┘  │
│                              │                                   │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │              FeatureServiceImpl                             │  │
│  │                                                             │  │
│  │  GetUserFeatures():                                        │  │
│  │  1. 提取 user_id                                           │  │
│  │  2. 随机生成 5~20 条用户行为日志                                │  │
│  │  3. 每条日志包含 10~50 个 uint32 特征向量                      │  │
│  │  4. 返回 UserFeatureResponse                               │  │
│  │                                                             │  │
│  │  GetSKUFeatures():                                         │  │
│  │  1. 提取 sku_ids 列表                                       │  │
│  │  2. 为每个 SKU 生成 10~30 字符的随机特征                       │  │
│  │  3. 返回 SKUFeatureResponse                                │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## 3. 目录结构

```
services/FeatureService/
├── CMakeLists.txt               # CMake 构建配置
├── Dockerfile                   # Docker 构建文件
├── server/
│   ├── include/
│   │   └── feature_server.h     # FeatureServiceImpl 声明
│   └── src/
│       ├── main.cpp             # 服务入口
│       └── feature_server.cpp   # 服务实现
```

## 4. Protobuf 协议定义

**文件**：`proto/feature.proto`

```protobuf
syntax = "proto3";
package feature;
option cc_generic_services = true;

// 特征类型枚举
enum FeatureType {
    KuaiRand = 0;
    UNKNOWN = 1;
}

// 快推用户特征请求
message KRUserFeatureRequest {
    uint64 user_id = 1;
    string req_data = 2;
}

// 快推用户日志
message KRUserLog {
    repeated uint32 vec = 1;
}

// 快推用户特征响应
message KRUserFeatureResponse {
    repeated KRUserLog user_logs = 1;
    string other = 2;
}

// 用户特征请求
message UserFeatureRequest {
    FeatureType feature_type = 1;
    KRUserFeatureRequest kr_feat_req = 2;
}

// 用户特征响应
message UserFeatureResponse {
    FeatureType feature_type = 1;
    KRUserFeatureResponse kr_feat_rsp = 2;
}

// SKU 特征请求
message SKUFeatureRequest {
    FeatureType feature_type = 1;
    repeated uint64 sku_ids = 2;
}

// 快推 SKU 特征
message KRSKUFeature {
    uint64 sku_id = 1;
    string feat = 2;
}

// SKU 特征响应
message SKUFeatureResponse {
    FeatureType feature_type = 1;
    repeated KRSKUFeature kr_sku_feats = 2;
}

// 特征服务定义
service FeatureService {
    rpc GetUserFeatures(UserFeatureRequest) returns (UserFeatureResponse);
    rpc GetSKUFeatures(SKUFeatureRequest) returns (SKUFeatureResponse);
}
```

**字段说明**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `user_id` | uint64 | 用户唯一标识 |
| `feature_type` | FeatureType | 特征类型（当前仅支持 KuaiRand） |
| `user_logs` | KRUserLog[] | 用户行为日志，每条包含 uint32 特征向量 |
| `other` | string | 附加信息 |
| `sku_ids` | uint64[] | SKU ID 列表 |
| `kr_sku_feats` | KRSKUFeature[] | SKU 特征列表 |

## 5. 组件详述

### 5.1 FeatureServiceImpl

核心服务类，处理特征请求。

**GetUserFeatures 请求处理流程**：

1. 从 `kr_feat_req` 提取 `user_id`
2. 随机生成 5~20 条 `KRUserLog`
3. 每条日志包含 10~50 个随机 uint32 特征值（范围 0~10000）
4. 设置 `other` 为 `"模拟特征_<user_id>"`
5. 设置 `feature_type` 为 `KuaiRand`

**GetSKUFeatures 请求处理流程**：

1. 遍历请求中的 `sku_ids` 列表
2. 为每个 SKU 生成 10~30 字符的随机特征字符串（a~z）
3. 设置 `feature_type` 为 `KuaiRand`

### 5.2 模拟数据生成参数

| 数据 | 参数 | 范围 |
|------|------|------|
| 用户日志条数 | 随机 | 5~20 条 |
| 每条日志向量长度 | 随机 | 10~50 个 uint32 |
| 向量元素值 | 随机 | 0~10000 |
| SKU 特征字符串长度 | 随机 | 10~30 字符 |
| SKU 特征字符集 | 随机 | a~z |

## 6. 容器集成方案

### 6.1 Dockerfile

基于 `brpc_base` 镜像，无特殊依赖。

### 6.2 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SERVER_PORT` | 8003 | 服务端口 |

## 7. 配置参数总表

| 参数名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `--server_port` | int32 | 8003 | 服务监听端口 |

## 8. 端口分配

| 服务 | 端口 | 说明 |
|------|------|------|
| FeatureService | 8003 | BRPC 服务端口 |

## 9. 数据流

### 9.1 GetUserFeatures

```
Upstream                   FeatureService
(FeatureClient)                (:8003)
     │                           │
     │  UserFeatureRequest       │
     │  (user_id, feature_type)  │
     │──────────────────────────▶│
     │                           │
     │                           │  1. 随机生成 user_logs
     │                           │     (5~20 条, 每条 10~50 向量)
     │                           │
     │  UserFeatureResponse      │
     │  (user_logs, other)       │
     │◀──────────────────────────│
```

### 9.2 GetSKUFeatures

```
Upstream                   FeatureService
(FeatureClient)                (:8003)
     │                           │
     │  SKUFeatureRequest        │
     │  (sku_ids: [100, 200])    │
     │──────────────────────────▶│
     │                           │
     │                           │  1. 为每个 SKU 生成随机特征
     │                           │     (10~30 字符 a~z)
     │                           │
     │  SKUFeatureResponse       │
     │  (kr_sku_feats: [...])    │
     │◀──────────────────────────│
```

## 10. 错误处理与边界情况

| 场景 | 行为 |
|------|------|
| **user_id 为 0 或缺失** | 正常返回随机特征数据 |
| **sku_ids 为空列表** | 返回空 `kr_sku_feats` |
| **并发请求** | 使用 `std::mt19937` 每实例独立随机数生成器，线程不安全（每个实例单线程处理） |
| **未知 feature_type** | 默认使用 `KuaiRand` 类型 |

## 11. 演进规划

| 版本 | 特性 |
|------|------|
| **V1.0** | 核心功能：用户/SKU 特征查询 + 模拟数据生成 |
| **V1.1** | 真实数据源：对接 KuaiRand 数据集，返回真实特征 |
| **V1.2** | 特征缓存：热点用户特征本地缓存 |
| **V1.3** | 批量查询：支持批量用户特征查询 |
| **V2.0** | 在线特征：对接实时特征计算平台 |
