# 前置计算服务 — PrecalcService

## 模块简介

PrecalcService 是推荐系统的前置计算层，负责将用户特征数据进行预计算，并将结果写入元戎 KVWorker 分布式缓存。下游的 RankServiceSub 通过 `user_feat_key` 从 KVWorker 读取前置计算结果进行精排打分。

本服务是数据管道的关键环节，连接了上游特征服务和下游精排服务。

## 目录结构

```
services/precalc/
├── DESIGN.md                    # 详细设计文档
├── README.md                    # 本文件
├── CMakeLists.txt               # CMake 构建配置
├── build.sh                     # 编译脚本
├── server/
│   ├── include/
│   │   └── precalc_server.h     # PrecalcServiceImpl 声明
│   └── src/
│       ├── main.cpp             # 服务入口
│       └── precalc_server.cpp   # 服务实现
├── client/
│   └── precalc_test_client.cpp  # 测试客户端
├── tests/
│   └── test_precalc.cpp         # 单元测试
```

## 编译命令

```bash
cd services/precalc_service
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 编译产物

| 二进制                   | 用途        |
| --------------------- | --------- |
| `precalc_server`      | 前置计算服务主程序 |
| `precalc_test_client` | 测试客户端     |

## 启动方式

### 启动 PrecalcService

```bash
./bin/precalc_server \
  --server_port=8004 \
  --kvworker_host=141.61.84.245 \
  --kvworker_port=31502 \
  --ttl_seconds=5
```

参数说明：

| 参数                         | 类型     | 默认值                  | 说明                     |
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

### 使用测试客户端

```bash
./bin/precalc_test_client --server=127.0.0.1:8004
```

## 容器搭建

```bash
docker run --name precalc-service precalc-image
```

环境变量配置：

| 变量              | 默认值           | 说明          |
| --------------- | ------------- | ----------- |
| `SERVER_PORT`   | 8004          | 服务端口        |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 主机 |
| `KVWORKER_PORT` | 31502         | KVWorker 端口 |
| `TTL_SECONDS`   | 5             | 数据 TTL      |

## 业务流程

```
       Upstream (FeatureService)
            │
            │ Precalculate(user_feat)
            ▼
   ┌─────────────────────┐         ┌──────────────────┐
   │  PrecalcService     │  Write  │  KVWorker         │
   │  (:8004)            │────────▶│  (:31501)         │
   │                     │         │                   │
   │  1. Extract key     │         │  key → 8.5MB data │
   │  2. Generate tensor │         │  TTL = 5s         │
   │  3. Write to KV     │         │                   │
   │  4. Return key+pay  │         └──────────────────┘
   └─────────────────────┘                │
                                          │ Read (by RankSub)
                                          ▼
                                 ┌──────────────────┐
                                 │  RankSubService  │
                                 └──────────────────┘
```

## 端口对照表

| 端口    | 服务                | 协议     | 说明       |
| ----- | ----------------- | ------ | -------- |
| 8004  | PrecalcService    | BRPC   | 前置计算服务端口 |
| 31502 | KVWorker (Recall) | 元戎 SDK | 分布式缓存端口  |

