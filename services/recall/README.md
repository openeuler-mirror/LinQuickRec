# 召回服务 — RecallService

## 模块简介

RecallService 是推荐系统的召回层，负责从海量商品池中筛选出与用户相关的候选 SKU 列表。本服务通过调用 vLLM 大语言模型（Qwen3-0.6B），将用户特征和行为日志转化为结构化的商品推荐列表。

本服务与 vLLM 同容器部署，通过 localhost HTTP 通信，实现最低延迟的模型推理调用。

## 目录结构

```
services/recall/
├── backup/
│   ├── brpc_client.cpp.backup
│   ├── brpc_server.cpp.backup
│   └── recommend.proto.backup
├── build.sh
├── CMakeLists.txt
├── DESIGN.md
├── README.md
├── client/
│   └── recall_test_client.cpp
├── server/
│   ├── include/
│   │   ├── recall_server.h
│   │   └── vllm_client.h
│   └── src/
│       ├── main.cpp
│       ├── recall_server.cpp
│       └── vllm_client.cpp
└── tests/
    └── test_recall.cpp
```

## 编译命令

| 依赖 | 版本要求 | 备注 |
|------|----------|------|
| CMake | >= 3.14 | 编译工具链 |
| brpc | >= 1.4 | `linquickrec/base:latest` 基础镜像已内置 |
| protobuf | >= 3.0 | `linquickrec/base:latest` 基础镜像已内置 |
| abseil-cpp | latest | `linquickrec/base:latest` 基础镜像已内置 |

### 脚本构建

```bash
cd services/recall
./build.sh              # 默认为 Release 构建
./build.sh release      # Release 构建
./build.sh debug        # Debug 构建
./build.sh clean        # 清理后构建
```

### 手动构建

```bash
cd services/recall
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make recall_server recall_test_client recall_test -j$(nproc)
```

### 编译产物

| 二进制 | 说明 |
|--------|------|
| `recall_server` | 召回服务主程序 |
| `recall_test_client` | 测试客户端 |
| `recall_test` | 单元测试 |

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

RecallService 与 vLLM 同容器部署，容器启动时自动启动 vLLM 并等待就绪。

### 构建镜像

```bash
docker build -t linquickrec/recall:latest \
  -f deploy/docker/recall/Dockerfile .
```

### 启动容器

```bash
docker run -d --name recall-service \
  --gpus all \
  -p 8000:8000 \
  -p 8001:8001 \
  linquickrec/recall:latest
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
