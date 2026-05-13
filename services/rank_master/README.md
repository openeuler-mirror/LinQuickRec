# 精排主图服务 — RankServiceMaster

## 模块简介

RankServiceMaster 是推荐系统精排层的主控节点，采用 Scatter-Gather 架构将候选商品集分发到多个 RankSub 子图进行并行打分，然后归并所有子图结果选出 Top-K 商品。

本服务支持水平扩展：通过 `docker-compose scale` 动态调整 RankSub 实例数量，Master 自动将 SKU 哈希分片到所有可用子图。

## 目录结构

```
services/rank_master/
├── DESIGN.md                        # 详细设计文档
├── README.md                        # 本文件
├── CMakeLists.txt                   # CMake 构建配置
├── build.sh                         # 编译脚本
├── client/
│   └── rank_master_test_client.cpp  # 测试客户端
├── server/
│   ├── include/
│   │   ├── rank_master_server.h     # RankMasterServiceImpl 声明
│   │   └── discovery_resolver.h     # DiscoveryResolver 声明
│   └── src/
│       ├── main.cpp                 # 服务入口
│       ├── rank_master_server.cpp   # 服务实现
│       └── discovery_resolver.cpp   # Discovery 服务发现实现
├── tests/
│   └── test_rank_master.cpp         # 单元测试
```
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
cd services/rank_master
./build.sh              # 默认为 Release 构建
./build.sh release      # Release 构建
./build.sh debug        # Debug 构建
./build.sh clean        # 清理后构建
```

### 手动构建

```bash
cd services/rank_master
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make rank_master_server rank_master_test_client rank_master_test -j$(nproc)
```

### 编译产物

| 二进制 | 说明 |
|--------|------|
| `rank_master_server` | 精排主图服务主程序 |
| `rank_master_test_client` | 测试客户端 |
| `rank_master_test` | 单元测试 |

## 启动方式

### 启动 RankServiceMaster

```bash
./bin/rank_master_server \
  --server_port=8004 \
  --discovery_addr=discovery-server:8100 \
  --sub_worker_service_type=rank_sub \
  --top_k=100
```

参数说明：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--server_port` | int32 | 8004 | 服务监听端口 |
| `--discovery_addr` | string | "" | Discovery 服务地址（空则使用 localhost fallback） |
| `--sub_worker_service_type` | string | "rank_sub" | RankSub 在 Discovery 中注册的服务类型名 |
| `--top_k` | int32 | 100 | 返回前 K 个商品 |
| `--sub_worker_timeout_ms` | int32 | 5000 | 子图调用超时时间（毫秒） |
| `--global_thread_pool_size` | int32 | 128 | 全局线程池大小，0 表示自动根据 CPU 核数计算 |

### 使用测试客户端

```bash
./bin/rank_master_test_client \
  --server=127.0.0.1:8004 \
  --sku_count=1000 \
  --payload_size_kb=100 \
  --tensor_size_mb=8.5 \
  --kvworker_host=141.61.84.245 \
  --kvworker_port=31502
```

测试客户端会自动：
1. 生成 16 位纯数字 `user_feat_key`
2. 生成 8.5MB tensor 数据
3. 将 tensor 写入 KVWorker（key = user_feat_key）
4. 生成随机 SKU 和 payload
5. 发送请求到 RankMaster

## 容器搭建

需先启动 RankSub 容器。

### 构建镜像

```bash
docker build -t linquickrec/rank-master:latest \
  -f deploy/docker/rank-master/Dockerfile .
```

### 启动容器

```bash
docker run -d --name rank-master \
  -p 8004:8004 \
  -e DISCOVERY_ADDR=discovery-server:8100 \
  linquickrec/rank-master:latest
```

### 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SERVER_PORT` | 8004 | 服务端口 |
| `DISCOVERY_ADDR` | discovery-server:8100 | Discovery 服务地址 |
| `SUB_WORKER_SERVICE_TYPE` | rank_sub | RankSub 在 Discovery 中注册的服务类型名 |
| `TOP_K` | 100 | 返回前 K 个商品 |
| `GLOBAL_THREAD_POOL_SIZE` | 0 | 全局线程池大小，0 表示自动计算 |

## 业务流程

```
       Client (Proxy)
            │
            │ Rank(key, skus, payload)
            ▼
   ┌─────────────────────────────────────────────┐
   │         RankServiceMaster (:8004)            │
   │                                              │
   │  1. Parse SKUs → [100456, 200789, ...]      │
   │  2. Hash distribute to N workers             │
   │                                              │
   │     ┌──────┐ ┌──────┐ ┌──────┐              │
   │     │ #0   │ │ #1   │ │ #N   │              │
   │     │async │ │async │ │async │              │
   │     └──┬───┘ └──┬───┘ └──┬───┘              │
   │        │        │        │                   │
   │  3. ───┼────────┼────────┼──▶ RankSub RPCs  │
   │        │        │        │                   │
   │  4. ◀──┼────────┼────────┼─── Collect scores│
   │        │        │        │                   │
   │  5. Top-K select (nth_element + sort)        │
   │     → [200789, 100456, ...]                  │
   └──────────────────────────────────────────────┘
```

## 端口对照表

| 端口 | 服务 | 协议 | 说明 |
|------|------|------|------|
| 8004 | RankServiceMaster | BRPC | 精排主图服务端口 |
| 8005 | RankServiceSub | BRPC | 精排子图服务端口 |
