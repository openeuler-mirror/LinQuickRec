# 召回服务设计方案

## 1. 设计目标

RecallService 是推荐系统的召回层，负责从海量商品池中筛选出与用户相关的候选集。本服务通过调用 vLLM 大语言模型，将用户特征和日志转化为结构化的 SKU ID 列表。

**核心需求**：
- 接收用户特征（user_id、行为日志、附加信息），调用大模型生成候选 SKU 列表
- 与 vLLM 服务同容器部署，通过 HTTP 协议通信，降低网络延迟
- 使用全局线程池处理并发请求，避免 BRPC 内置线程池瓶颈
- 支持可配置的 SKU 数量、模型路径和超时时间



## 2. 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    RecallService Container                       │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    vLLM Server (:8000)                      │  │
│  │  ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐  │  │
│  │  │ Qwen3-0.6B  │  │ HTTP Server  │  │  /v1/chat/       │  │  │
│  │  │   Model     │  │  (uvicorn)   │  │  completions     │  │  │
│  │  └─────────────┘  └──────────────┘  └──────────────────┘  │  │
│  └──────────────────────────┬────────────────────────────────┘  │
│                              │ HTTP POST (localhost:8000)       │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                RecallService (:8001)                        │  │
│  │                                                             │  │
│  │  ┌─────────────┐    ┌──────────────┐    ┌──────────────┐  │  │
│  │  │  BRPC RPC   │    │  Protocol    │    │   Global     │  │  │
│  │  │  Handler    │───▶│  Converter   │───▶│  Thread Pool │  │  │
│  │  │             │    │  Proto↔JSON  │    │              │  │  │
│  │  └─────────────┘    └──────────────┘    └──────┬───────┘  │  │
│  │                                                 │          │  │
│  │  ┌──────────────────────────────────────────────▼───────┐  │  │
│  │  │              vLLM Client (HTTP)                       │  │  │
│  │  │  1. Build prompt from user features                  │  │  │
│  │  │  2. POST /v1/chat/completions                        │  │  │
│  │  │  3. Parse SKU IDs from LLM response                  │  │  │
│  │  └──────────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  entrypoint.sh                                             │  │
│  │  1. Start vLLM (background)                                │  │
│  │  2. Wait for vLLM health check                             │  │
│  │  3. Start recall_server (foreground)                       │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
         ▲
         │ BRPC RPC (RecallService.Recall)
         │
┌────────┴────────┐
│  Upstream Caller │
│  (e.g. Proxy)    │
└─────────────────┘
```

## 3. 目录结构

```
services/recall/
├── DESIGN.md                    # 本文档
├── README.md                    # 模块介绍与使用说明
├── CMakeLists.txt               # CMake 构建配置
├── build.sh                     # 编译脚本
├── server/
│   ├── include/
│   │   └── recall_server.h      # RecallServiceImpl 声明
│   └── src/
│       ├── main.cpp             # 服务入口
│       └── recall_server.cpp    # 服务实现
└── client/
    └── recall_test_client.cpp   # 测试客户端
```

## 4. Protobuf 协议定义

**文件**：`proto/recall.proto`

```protobuf
syntax = "proto3";
package recall;
option cc_generic_services = true;

message KRUserLog {
    repeated uint32 vec = 1;
}

message RecallRequest {
    uint64 user_id = 1;
    repeated KRUserLog user_logs = 2;
    string other = 3;
}

message RecallResponse {
    repeated uint64 sku_ids = 1;
}

service RecallService {
    rpc Recall(RecallRequest) returns (RecallResponse);
}
```

**字段说明**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `user_id` | uint64 | 用户唯一标识 |
| `user_logs` | KRUserLog[] | 用户行为日志，每条包含特征向量 |
| `other` | string | 附加信息（JSON 格式） |
| `sku_ids` | uint64[] | 召回的候选 SKU ID 列表 |

## 5. 组件详述

### 6.1 RecallServiceImpl

核心服务类，处理召回请求。

**请求处理流程**：

1. 接收 `RecallRequest`，提取 `user_id`、`user_logs`、`other`
2. 调用 `proto_to_json()` 将 Proto 请求转为 JSON
3. 调用 `build_vllm_request()` 构建符合 Qwen3-0.6B 的 Chat Completion 请求
4. 通过 BRPC HTTP Channel 向 vLLM 发送 POST 请求
5. 调用 `parse_vllm_response()` 解析 LLM 返回的 SKU ID
6. 填充 `RecallResponse` 返回

### 6.2 Protocol Converter（协议转换层）

三个核心函数实现 Proto ↔ JSON 的双向转换：

| 函数 | 输入 | 输出 | 说明 |
|------|------|------|------|
| `proto_to_json` | RecallRequest | JSON string | 将 Proto 请求序列化为 JSON |
| `build_vllm_request` | JSON string | JSON string | 构建 Chat Completion API 请求体 |
| `parse_vllm_response` | JSON string | RecallResponse | 从 LLM 响应中提取 SKU ID |

### 6.3 Prompt 工程

**System Prompt**：
```
你是一个搜推广助手，请根据用户特征和日志返回推荐的 SKU ID 列表。
请恰好生成 {sku_count} 个 SKU ID，不要多也不要少。
返回格式：用逗号分隔的数字，例如：12345,67890,11111,...
```

**User Prompt**：
```
用户请求数据：{request_json}
```

**解析策略**：
- LLM 返回的 content 按逗号分隔
- 去除空格和引号
- 使用 `stoull` 转换为 uint64
- 解析失败的 token 跳过并记录 WARNING

### 6.4 vLLM Client

通过 BRPC HTTP Channel 与 vLLM 通信：

```
RecallService ──HTTP POST──▶ vLLM (:8000)
                           /v1/chat/completions
                           Content-Type: application/json
                           Body: {"model": "...", "messages": [...]}
                           
RecallService ◀──JSON──── vLLM
                           {"choices": [{"message": {"content": "123,456,..."}}]}
```

**关键配置**：
- 连接方式：pooled（连接池复用）
- 超时时间：`vllm_timeout_ms`（默认 5000ms）
- 协议：HTTP/1.1

### 6.5 启动流程（entrypoint.sh）

```
1. 启动 vLLM（后台进程）
   /app/run_vllm.sh &
   
2. 等待 vLLM 就绪（最多 120 秒）
   curl http://127.0.0.1:8000/health
   
3. 启动 RecallService（前台进程）
   ./bin/recall_server --server_port=8001 ...
   
4. RecallService 退出时，终止 vLLM
```

## 6. 容器集成方案

### 7.1 Dockerfile

基于 `brpc_base:v1.2`，额外安装：
- vLLM（从 .whl 文件安装）
- Qwen3-0.6B 模型
- RapidJSON

### 7.2 GPU 资源

Recall 容器需要 GPU 资源，docker-compose 中配置：
```yaml
deploy:
  resources:
    reservations:
      devices:
        - driver: nvidia
          count: 1
          capabilities: [gpu]
```

### 7.3 端口映射

| 容器端口 | 宿主机端口 | 用途 |
|----------|-----------|------|
| 8001 | 8001 | RecallService BRPC 端口 |
| 8000 | 8000 | vLLM HTTP 端口（调试用） |

## 7. 配置参数总表

| 参数名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `--server_port` | int32 | 8001 | 服务监听端口 |
| `--vllm_base_url` | string | "http://127.0.0.1:8000" | vLLM 服务基础 URL |
| `--vllm_endpoint` | string | "/v1/chat/completions" | vLLM 聊天接口端点 |
| `--model_name` | string | "/workspace/share/Qwen3-0.6B/" | 模型路径 |
| `--vllm_timeout_ms` | int32 | 5000 | vLLM 请求超时时间（毫秒） |
| `--sku_count` | int32 | 1000 | 返回的 SKU ID 数量 |

## 8. 端口分配

| 服务 | 端口 | 说明 |
|------|------|------|
| RecallService | 8001 | BRPC 服务端口 |
| vLLM | 8000 | HTTP API 端口（容器内部） |

## 9. 数据流

```
Client                   RecallService                    vLLM
  │                          │                              │
  │  RecallRequest           │                              │
  │  (user_id, logs, other)  │                              │
  │─────────────────────────▶│                              │
  │                          │  proto_to_json()              │
  │                          │  build_vllm_request()         │
  │                          │                              │
  │                          │  HTTP POST /v1/chat/          │
  │                          │  completions                  │
  │                          │─────────────────────────────▶│
  │                          │                              │
  │                          │       LLM Inference          │
  │                          │       (Qwen3-0.6B)           │
  │                          │                              │
  │                          │  JSON Response               │
  │                          │  (choices[0].message.content) │
  │                          │◀─────────────────────────────│
  │                          │                              │
  │                          │  parse_vllm_response()        │
  │                          │  → [12345, 67890, ...]       │
  │                          │                              │
  │  RecallResponse          │                              │
  │  (sku_ids: [...])        │                              │
  │◀─────────────────────────│                              │
```

## 10. 错误处理与边界情况

| 场景 | 行为 |
|------|------|
| **vLLM 启动慢** | entrypoint.sh 等待最多 120 秒，超时则容器退出 |
| **vLLM 请求超时** | 返回错误响应，`success=false`，`error_message="vLLM service error"` |
| **vLLM 返回无效 JSON** | `parse_vllm_response` 返回 false，记录 ERROR 日志 |
| **LLM 输出格式错误** | 逐 token 解析，跳过无效 token，记录 WARNING |
| **LLM 返回 SKU 数量不足** | 返回已解析的 SKU，数量可能少于 `sku_count` |
| **LLM 返回 SKU 数量过多** | 全部返回，不做截断 |
| **vLLM 进程崩溃** | RecallService 后续请求全部失败，需容器重启 |
| **GPU OOM** | vLLM 自动处理（显存不足时拒绝请求），RecallService 收到超时 |

## 11. 演进规划

| 版本 | 特性 |
|------|------|
| **V1.0** | 核心功能：vLLM 调用 + Prompt 工程 + SKU 解析 |
| **V1.1** | 多模型支持：支持切换不同 LLM（Qwen2.5-7B 等） |
| **V1.2** | 流式响应：支持 vLLM streaming 模式，降低首 token 延迟 |
| **V1.3** | 结果缓存：相同 user_id 短时间内返回缓存结果 |
| **V2.0** | 多路召回：支持向量召回 + LLM 召回融合策略 |
