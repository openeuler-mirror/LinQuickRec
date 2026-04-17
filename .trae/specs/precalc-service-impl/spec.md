# PrecalcService 实现规范

## Why
实现前置计算服务，支持向精排 KVWorker 写入前置计算结果，为排序服务提供用户特征编码。需要支持可配置的结果大小、TTL 自动删除，并与 Proxy 服务通过 BRPC 通信。

## What Changes
- ✅ 修改 recall_server.cpp，支持自定义 SKU ID 数量（默认 1000）
- ✅ 创建 API 接口文档，详细说明各服务接口
- ✅ 更新 README.md，反映最新架构
- ✅ 实现 PrecalcService 服务端，支持：
  - 向精排 KVWorker 写入前置计算结果（默认 8.5MB）
  - 支持 5 秒 TTL 自动删除
  - 生成 key 格式：user_id + 时间戳
  - 通过元戎（openYuanrong）与 KVWorker 通信
- ✅ 实现 PrecalcService 客户端，用于功能测试
- ✅ 创建 PrecalcService CMakeLists.txt

## Impact
- **Affected specs**: RecallService, PrecalcService, ProxyService
- **Affected code**: 
  - `services/recall/server/recall_server.cpp`
  - `services/PrecalcService/` (新建)
  - `README.md`
  - `docs/API.md` (新建)

## ADDED Requirements

### Requirement: SKU 数量可配置
**场景**: RecallService 需要根据需求返回指定数量的 SKU ID

**WHEN**: 请求中包含 `top_k` 参数
**THEN**: 返回的 SKU ID 数量等于 `top_k` 的值，默认 1000

### Requirement: PrecalcService 功能
**场景**: 前置计算服务接收用户特征，生成编码并写入 KVWorker

**WHEN**: PrecalcService 收到 PrecalcRequest
**THEN**: 
- 生成 8.5MB（可配置）的前置计算结果
- 通过元戎写入精排 KVWorker
- 设置 5 秒 TTL
- 返回 `user_feat_key`（格式：user_id + 时间戳）

### Requirement: PrecalcService 通信
**场景**: PrecalcService 与 Proxy 和 KVWorker 的通信

**WHEN**: 服务间通信
**THEN**:
- 与 Proxy 服务：BRPC 协议
- 与 KVWorker：元戎（openYuanrong）协议

### Requirement: 模拟逻辑
**场景**: 前置计算过程使用模拟逻辑

**WHEN**: 生成前置计算结果
**THEN**: 使用确定的模拟算法（需用户确认）

## MODIFIED Requirements

### Requirement: Proto 消息定义
**消息结构**（已更新）:
```protobuf
message PrecalcRequest {
    string user_feat = 1; // 100KB
}

message PrecalcResponse {
    string user_feat_key = 1;
    string payload = 2; // 100KB
}
```

### Requirement: Key 生成规则
**格式**: `user_id + "_" + timestamp`
- `user_id`: 从 `user_feat` 中提取
- `timestamp`: 当前时间戳（微秒）

## REMOVED Requirements
无
