# 召回服务 — RecallService

## 模块简介

RecallService 是推荐系统的召回层，负责从海量商品池中筛选出与用户相关的候选 SKU 列表。本服务通过调用 vLLM 大语言模型（Qwen3-0.6B），将用户特征和行为日志转化为结构化的商品推荐列表。

本服务与 vLLM 同容器部署，通过 localhost HTTP 通信，实现最低延迟的模型推理调用。

## 目录结构

```
services/recall/
├── backup/
├── build.sh
├── CMakeLists.txt
├── DESIGN.md
├── README.md
├── server/
│   ├── include/
│   │   ├── recall_server.h
│   │   └── vllm_client.h
│   └── src/
│       ├── main.cpp
│       ├── recall_server.cpp
│       └── vllm_client.cpp
└── tests/
    ├── recall_integration_test.cpp
    └── recall_test_client.cpp
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
make recall_server recall_test_client recall_integration_test -j$(nproc)
```

### 编译产物

| 二进制 | 说明 |
|--------|------|
| `recall_server` | 召回服务主程序 |
| `recall_test_client` | 测试客户端 |
| `recall_integration_test` | 集成测试 |

## 启动方式

### 启动 RecallService

```bash
./bin/recall_server --server_port=8002 --vllm_base_url=http://127.0.0.1:8000
```

参数说明：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--server_port` | int32 | 8002 | 服务监听端口 |
| `--enable_vllm` | bool | true | 是否启用 vLLM；false 时使用 novllm 模拟召回 |
| `--sku_count` | int32 | 1000 | 返回的 SKU ID 数量 |
| `--vllm_base_url` | string | "http://127.0.0.1:8000" | vLLM 服务基础 URL |
| `--vllm_endpoint` | string | "/v1/chat/completions" | vLLM 聊天接口端点 |
| `--model_name` | string | "/workspace/share/Qwen3-0.6B/" | vLLM 模型路径 |
| `--vllm_timeout_ms` | int32 | 100000 | vLLM 请求超时时间（毫秒） |
| `--kvcache_hit_rate` | double | 0.5 | novllm 模式缓存命中率，范围 [0.0, 1.0] |
| `--kvcache_hit_sleep_time_ms` | int32 | 10 | novllm 模式缓存命中模拟耗时 (ms) |
| `--kvcache_miss_sleep_time_ms` | int32 | 100 | novllm 模式缓存未命中模拟耗时 (ms) |
| `--server_num_threads` | int32 | 0 | 服务端 bthread 线程数，0=BRPC 默认(CPU 核数) |
| `--server_idle_timeout_sec` | int32 | -1 | 空闲连接超时 (秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 最大并发请求数，0=不限制 |

novllm 模式会在第一次请求前写入固定 KV key `rc:novllm:global_seed`。随后按 `--kvcache_hit_rate` 随机选择命中或未命中：命中路径先 `Exist` 再 `Get` 固定 key；未命中路径对另一个临时 key 执行 `Exist`，再用 `Create` + `Set` 重写固定 key。返回 SKU 由随机数生成，不依赖 KV value 内容。

**vLLM 通道参数：**

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--vllm_timeout_ms` | int32 | 100000 | vLLM 通道超时 (ms) |
| `--vllm_connection_type` | string | "single" | vLLM 通道连接类型 |
| `--vllm_max_retry` | int32 | 3 | vLLM 通道 BRPC 重试次数 |
| `--vllm_connect_timeout_ms` | int32 | -1 | vLLM 通道建连超时 (ms)，-1=禁用 |
| `--vllm_backup_request_ms` | int32 | -1 | vLLM 通道 backup request (ms)，-1=禁用 |

### 使用测试客户端

```bash
./bin/recall_test_client --server=127.0.0.1:8002 --user_id=12345
```

## 测试方法

### 集成测试

集成测试在单进程内启动 mock vLLM（raw socket HTTP 服务器）+ 真实 `RecallServiceImpl`（BRPC server），通过 `RecallService_Stub.Recall()` 发送真实 RPC 并用 `assert()` 验证结果。无需外部 vLLM 或其他依赖服务。

```bash
# 运行集成测试
./bin/recall_integration_test
```

覆盖 5 个场景：

| 场景 | 验证内容 |
|------|----------|
| vLLM 返回有效 SKU | 全链路成功，sku_ids 值正确 |
| vLLM 不可达 | 返回 error_code = `VLLM_REQUEST_FAILED` (0x03030002) |
| vLLM 返回畸形 JSON | 返回 error_code = `VLLM_RESPONSE_PARSE_FAILED` (0x03030003) |
| vLLM 返回空 choices | 返回 error_code = `VLLM_RESPONSE_PARSE_FAILED` (0x03030003) |
| vLLM 返回数不足 + 补齐 | 返回 target 数量的不重复 SKU |

#### 测试原理

测试在一个进程内启动 2 个 server，模拟完整的 vLLM 调用链路：

```
┌──────────────────────── Single Process ────────────────────────┐
│                                                                 │
│  ┌───────────────────────────┐  ┌────────────────────────────┐ │
│  │  MockVllmServer (:18000)  │  │  RecallServiceImpl         │ │
│  │  (raw socket HTTP)        │  │  (:18002, BRPC)            │ │
│  │                           │  │                            │ │
│  │  POST /v1/chat/completions│  │  ┌──────────────────────┐  │ │
│  │  → 返回预制 JSON          │◄─┤│  VllmClient            │  │ │
│  │                           │  │  │  → HTTP POST          │  │ │
│  └───────────────────────────┘  │  └──────────────────────┘  │ │
│                                  └─────────────┬──────────────┘ │
│                                                │                │
│  ┌─────────────────────────────────────────────▼──────────────┐ │
│  │  RecallService_Stub.Recall()                               │ │
│  │  → assert(error_code == expected)                          │ │
│  │  → assert(sku_ids_size == expected)                        │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

每个场景内部：

1. 启动 MockVllmServer（可选，mock 不可达场景跳过），配置预制 JSON 响应
2. 设置 gflags（`--vllm_base_url` 指向 mock、`--sku_count=5`、禁用重试和 sleep）
3. 创建 `RecallServiceImpl` 实例，注册到 `brpc::Server` 并启动
4. 通过 `RecallService_Stub.Recall()` 发送真实 BRPC 请求
5. `assert()` 逐一验证：RPC 成功、`error_code`、`sku_ids` 数量和内容、无重复
6. 任意 `assert` 失败 → 程序立即 abort
7. 停止所有 Server，清理

### 手动测试

需要 recall 服务在运行中：

```bash
./bin/recall_test_client \
    --server="127.0.0.1:8002" \
    --user_id=12345
```

输出示例：

```
========================================
Recall Service Client Test
========================================
Request:
  user_id: 12345
  user_logs count: 3
  payload size: 102400 bytes (100 KB)
Sending request to 127.0.0.1:8002

========================================
Response:
========================================
SKU IDs count: 1000
First 20 SKU IDs: 123456, 234567, ...
========================================
Test completed successfully!
========================================
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
  -p 8002:8002 \
  linquickrec/recall:latest
```

## 业务流程

```
       Client (Proxy)
            │
            │ Recall(user_id, logs, payload)
            ▼
   ┌─────────────────────┐
   │   RecallService     │
   │   (:8002)           │
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
| 8002 | RecallService | BRPC | 召回服务 RPC 端口 |
| 8000 | vLLM | HTTP | 大模型推理 API（容器内部） |
