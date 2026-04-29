# 前置计算服务设计方案

## 1. 设计目标

PrecalcService 是推荐系统的前置计算层，负责将用户特征数据（user\_feat）进行预计算，并将结果写入元戎 KVWorker 分布式缓存，供下游 RankServiceSub 读取使用。

**核心需求**：

- 接收用户特征数据，生成前置计算结果（模拟 8.5MB tensor）
- 将计算结果以 `user_feat_key` 为键写入元戎 KVWorker
- 设置合理的 TTL，确保数据在下游消费前有效
- 返回 `user_feat_key` 和 payload 给调用方



## 2. 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                      PrecalcService (:8004)                      │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    BRPC RPC Handler                        │  │
│  │  Precalculate(PrecalcRequest) → PrecalcResponse           │  │
│  └──────────────────────────┬────────────────────────────────┘  │
│                              │                                   │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                  Global Thread Pool                         │  │
│  │  submit(process_precalc_request)                           │  │
│  └──────────────────────────┬────────────────────────────────┘  │
│                              │                                   │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │              process_precalc_request()                      │  │
│  │                                                             │  │
│  │  1. Validate user_feat (non-empty)                         │  │
│  │  2. Extract user_feat_key (first 16 chars)                  │  │
│  │  3. Generate precalc_result (8.5MB random tensor)          │  │
│  │  4. Write to KVWorker (Create + memcpy + Set)              │  │
│  │  5. Generate payload (100KB)                               │  │
│  │  6. Return response                                        │  │
│  └──────────────────────────┬────────────────────────────────┘  │
│                              │                                   │
└──────────────────────────────┼───────────────────────────────────┘
                               │
                               │ KVWorker Write
                               │ (Create + Set)
                               ▼
               ┌───────────────────────────────┐
               │     元戎 KVWorker Cluster      │
               │     (:31501)                   │
               │                                │
               │  key: "1234567812345678"                 │
               │  value: 8.5MB tensor data      │
               │  TTL: 5 seconds                │
               └───────────────────────────────┘
```

## 3. 目录结构

```
services/PrecalcService/
├── DESIGN.md                    # 本文档
├── README.md                    # 模块介绍与使用说明
├── CMakeLists.txt               # CMake 构建配置
├── build.sh                     # 编译脚本
├── server/
│   ├── include/
│   │   └── precalc_server.h     # PrecalcServiceImpl 声明
│   └── src/
│       ├── main.cpp             # 服务入口
│       └── precalc_server.cpp   # 服务实现
└── client/
    └── precalc_test_client.cpp  # 测试客户端
```

## 4. Protobuf 协议定义

**文件**：`proto/precalc.proto`

```protobuf
syntax = "proto3";
package precalc;
option cc_generic_services = true;

message PrecalcRequest {
    string user_feat = 1;  // 用户特征数据（100KB）
}

message PrecalcResponse {
    string user_feat_key = 1;  // KVWorker 中的键
    string payload = 2;        // 负载数据（100KB）
}

service PrecalcService {
    rpc Precalculate(PrecalcRequest) returns (PrecalcResponse);
}
```

**字段说明**：

| 字段              | 大小       | 说明                               |
| --------------- | -------- | -------------------------------- |
| `user_feat`     | \~100KB  | 用户特征原始数据                         |
| `user_feat_key` | 16 bytes | KVWorker 存储键（user\_feat 前 16 字符） |
| `payload`       | \~100KB  | 模拟负载数据                           |

## 5. 组件详述

### 6.1 PrecalcServiceImpl

核心服务类，处理前置计算请求。

**请求处理流程**：

1. 验证 `user_feat` 非空
2. 提取 `user_feat_key`：取 `user_feat` 的前 16 个字符
3. 生成前置计算结果：`generate_precalc_result(FLAGS_precalc_result_size_mb)`
4. 写入 KVWorker：
   - `KVClient::Create(key, size, param, buffer)` 分配缓冲区
   - `memcpy(buffer, data, size)` 填充数据
   - `KVClient::Set(buffer)` 提交写入
5. 生成 payload：`generate_random_string(FLAGS_payload_size_kb * 1024)`
6. 返回 `PrecalcResponse`

### 6.2 KVWorker 写入流程

```
PrecalcService                 KVWorker
     │                            │
     │  ConnectOptions            │
     │  host: 141.61.84.245       │
     │  port: 31502               │
     │                            │
     │  Init()                    │
     │───────────────────────────▶│
     │◀─────── OK ───────────────│
     │                            │
     │  Create(key, size, param)  │
     │───────────────────────────▶│
     │◀─── Buffer ───────────────│
     │                            │
     │  memcpy(Buffer, Data)      │
     │                            │
     │  Set(Buffer)               │
     │───────────────────────────▶│
     │◀─────── OK ───────────────│
```

**SetParam 配置**：

| 参数          | 值               | 说明                 |
| ----------- | --------------- | ------------------ |
| `ttlSecond` | 5               | TTL 5 秒，下游需在此时间内读取 |
| `writeMode` | NONE\_L2\_CACHE | 不写入 L2 缓存          |
| `existence` | NONE            | 不检查存在性             |
| `cacheType` | MEMORY          | 纯内存缓存              |

### 6.3 user\_feat\_key 截取规则

```
user_feat = "A1B2C3D4E5F6G7H8..."
            │────────────────│
                前 16 字符
user_feat_key = "A1B2C3D4E5F6G7H8"

若 user_feat 长度 < 16，则取全部
```

**设计考量**：

- 6 字符长度足以区分不同用户特征
- 固定长度便于下游解析
- 与 RankServiceSub 的 KVWorker 读取键一致

### 6.4 数据大小配置

| 数据             | 默认大小     | 配置参数                       |
| -------------- | -------- | -------------------------- |
| 前置计算结果（tensor） | 8.5 MB   | `--precalc_result_size_mb` |
| payload        | 100 KB   | `--payload_size_kb`        |
| user\_feat（输入） | \~100 KB | 由调用方决定                     |

## 6. 容器集成方案

### 7.1 Dockerfile

基于 `brpc_base:v1.3`，元戎 SDK 已包含在基础镜像中。

### 7.2 环境变量

| 变量              | 默认值           | 说明          |
| --------------- | ------------- | ----------- |
| `SERVER_PORT`   | 8004          | 服务端口        |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 主机 |
| `KVWORKER_PORT` | 31502         | KVWorker 端口 |
| `TTL_SECONDS`   | 5             | 数据 TTL      |

## 7. 配置参数总表

| 参数名                        | 类型     | 默认值                  | 说明                     |
| -------------------------- | ------ | -------------------- | ---------------------- |
| `--server_port`            | int32  | 8004                 | 服务监听端口                 |
| `--kvworker_host`          | string | "141.61.84.245"      | 元戎 KVWorker 主机地址       |
| `--kvworker_port`          | int32  | 31502                | 元戎 KVWorker 端口         |
| `--etcd_address`           | string | "141.61.84.245:2379" | ETCD 地址                |
| `--precalc_result_size_mb` | double | 8.5                  | 前置计算结果大小（MB）           |
| `--ttl_seconds`            | int32  | 5                    | TTL 时间（秒）              |
| `--user_feat_key_size_kb`  | int32  | 100                  | user\_feat\_key 大小（KB） |
| `--enable_timing_stats`    | bool   | true                 | 是否启用详细时延统计             |
| `--payload_size_kb`        | int32  | 100                  | payload 大小（KB）         |

## 8. 端口分配

| 服务                | 端口    | 说明             |
| ----------------- | ----- | -------------- |
| PrecalcService    | 8004  | BRPC 服务端口      |
| KVWorker (Recall) | 31502 | 元戎 KVWorker 端口 |

## 9. 数据流

```
Upstream                  PrecalcService                KVWorker
(PrecalcClient)               (:8004)                  (:31502)
     │                           │                        │
     │  PrecalcRequest           │                        │
     │  (user_feat: 100KB)       │                        │
     │──────────────────────────▶│                        │
     │                           │                        │
     │                           │  1. Extract key        │
     │                           │     "A1B2C3D4E5F6G7H8" │
     │                           │                        │
     │                           │  2. Generate tensor    │
     │                           │     (8.5MB random)     │
     │                           │                        │
     │                           │  3. Create(key, 8.5MB) │
     │                           │───────────────────────▶│
     │                           │◀── Buffer ────────────│
     │                           │                        │
     │                           │  4. memcpy(data)       │
     │                           │  5. Set(buffer)        │
     │                           │───────────────────────▶│
     │                           │◀── OK ────────────────│
     │                           │                        │
     │  PrecalcResponse          │                        │
     │  (key: "A1B2C3D4E5F6G7H8",│                        │
     │   payload: 100KB)         │                        │
     │◀──────────────────────────│                        │
```

## 10. 错误处理与边界情况

| 场景                     | 行为                                               |
| ---------------------- | ------------------------------------------------ |
| **空 user\_feat**       | 返回空 `user_feat_key` 和空 `payload`，记录 ERROR        |
| **KVWorker Init 失败**   | 返回空响应，记录 ERROR                                   |
| **KVWorker Create 失败** | 返回空响应，记录 ERROR                                   |
| **KVWorker Set 失败**    | 返回空响应，记录 ERROR                                   |
| **user\_feat 长度 < 16** | 取全部字符作为 key                                      |
| **TTL 过期**             | 下游 RankSub 读取时返回 key not found                   |
| **KVWorker 不可达**       | 所有请求失败，需检查网络或 KVWorker 状态                        |
| **大数据量写入慢**            | 记录 kvwrite\_cost 耗时，可通过 `enable_timing_stats` 开启 |

## 11. 演进规划

| 版本       | 特性                               |
| -------- | -------------------------------- |
| **V1.0** | 核心功能：KVWorker 写入 + TTL 管理 + 时延统计 |
| **V1.1** | 真实计算：替换随机 tensor 为实际模型推理结果       |
| **V1.2** | 批量写入：支持一次请求写入多个 key              |
| **V1.3** | 写入确认：支持写入后读回验证                   |
| **V2.0** | 异步写入：KVWorker 写入异步化，降低请求延迟       |

