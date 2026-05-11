# 召回服务 — RecallService

## 模块简介

RecallService 是推荐系统的召回层，负责从海量商品池中筛选出与用户相关的候选 SKU 列表。本服务通过调用 vLLM 大语言模型（Qwen3-0.6B），将用户特征和行为日志转化为结构化的商品推荐列表。

本服务与 vLLM 同容器部署，通过 localhost HTTP 通信，实现最低延迟的模型推理调用。

## 目录结构

```
services/recall/
├── DESIGN.md                    # 详细设计文档
├── README.md                    # 本文件
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

## 编译命令

```bash
cd services/recall
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 编译产物

| 二进制 | 用途 |
|--------|------|
| `recall_server` | 召回服务主程序 |
| `recall_test_client` | 测试客户端 |

## 启动方式

### 启动 RecallService

```bash
./bin/recall_server --server_port=8001 --vllm_base_url=http://127.0.0.1:8000
```

参数说明：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--server_port` | int32 | 8001 | 服务监听端口 |
| `--vllm_base_url` | string | "http://127.0.0.1:8000" | vLLM 服务基础 URL |
| `--vllm_endpoint` | string | "/v1/chat/completions" | vLLM 聊天接口端点 |
| `--model_name` | string | "/workspace/share/Qwen3-0.6B/" | 模型路径 |
| `--vllm_timeout_ms` | int32 | 5000 | vLLM 请求超时时间（毫秒） |
| `--sku_count` | int32 | 1000 | 返回的 SKU ID 数量 |

### 使用测试客户端

```bash
./bin/recall_test_client --server=127.0.0.1:8001 --user_id=12345
```

## 容器搭建

RecallService 与 vLLM 同容器部署，容器启动时先启动 vLLM，再启动 RecallService：

```bash
docker run --gpus all --init --name recall-service \
  recall-image \
  sh -c "/app/run_vllm.sh & \
         sleep 30 && \
         /app/recall_server --server_port=8001 & \
         wait"
```

或使用 entrypoint.sh 自动管理启动顺序：

```bash
docker run --gpus all --name recall-service recall-image
```

## 业务流程

```
       Client (Proxy)
            │
            │ Recall(user_id, logs, other)
            ▼
   ┌─────────────────────┐
   │   RecallService     │
   │   (:8001)           │
   │                     │
   │  1. Proto → JSON    │
   │  2. Build Prompt    │──── HTTP POST ────▶ vLLM (:8000)
   │  3. Parse SKU IDs   │◀── JSON Response ──
   │  4. Return Response │
   └─────────────────────┘
```

## 端口对照表

| 端口 | 服务 | 协议 | 说明 |
|------|------|------|------|
| 8001 | RecallService | BRPC | 召回服务 RPC 端口 |
| 8000 | vLLM | HTTP | 大模型推理 API（容器内部） |
