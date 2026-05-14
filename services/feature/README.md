# 特征服务 — FeatureService

## 模块简介

FeatureService 是推荐系统的特征层，负责提供用户特征和 SKU 特征数据，供上游服务（如 RecallService）在召回和排序阶段使用。当前为模拟实现，通过随机数生成测试特征数据，用于端到端流水线验证。支持 KuaiRand 特征类型。

## 目录结构

```
services/feature/
├── CMakeLists.txt               # CMake 构建配置
├── build.sh                     # 编译脚本
├── server/
│   ├── include/
│   │   └── feature_server.h     # FeatureServiceImpl 声明
│   └── src/
│       ├── main.cpp             # 服务入口
│       └── feature_server.cpp   # 服务实现
```

## 编译命令

```bash
cd services/feature
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 编译产物

| 二进制 | 说明 |
|---|---|
| `feature_server` | 特征服务主程序 |

## 启动方式

### 启动 FeatureService

```bash
./bin/feature_server --server_port=8001
```

参数说明：

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--server_port` | int32 | 8001 | 服务监听端口 |
| `--server_num_threads` | int32 | 0 | 服务端 bthread 线程数，0=BRPC 默认(CPU 核数) |
| `--server_timeout_ms` | int32 | 0 | 服务端处理超时上限 (ms)，0=不限制 |
| `--server_idle_timeout_sec` | int32 | -1 | 空闲连接超时 (秒)，-1=BRPC 默认 |
| `--server_max_concurrency` | int32 | 0 | 最大并发请求数，0=不限制 |

### RPC 接口

| 方法 | 请求 | 响应 | 说明 |
|---|---|---|---|
| `GetUserFeatures` | UserFeatureRequest | UserFeatureResponse | 获取用户行为日志特征 |
| `GetSKUFeatures` | SKUFeatureRequest | SKUFeatureResponse | 获取 SKU 特征数据 |

## 容器搭建

```bash
docker run --name feature-service feature-image
```

环境变量配置：

| 变量 | 默认值 | 说明 |
|---|---|---|
| `SERVER_PORT` | 8001 | 服务端口 |

## 业务流程

```
       Upstream (Proxy)
            │
            │ GetUserFeatures(user_id)
            ▼
   ┌─────────────────────┐
   │   FeatureService    │
   │   (:8001)           │
   │                     │
   │  随机生成 user_logs │
   │  (5~20 条日志)      │
   │  每条 10~50 维向量   │
   │                     │
   │  返回 UserFeature   │
   └─────────────────────┘
```

## 端口对照表

| 端口 | 服务 | 协议 | 说明 |
|---|---|---|---|
| 8001 | FeatureService | BRPC | 特征服务端口 |
