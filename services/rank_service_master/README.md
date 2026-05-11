# 精排主图服务 — RankServiceMaster

## 模块简介

RankServiceMaster 是推荐系统精排层的主控节点，采用 Scatter-Gather 架构将候选商品集分发到多个 RankSub 子图进行并行打分，然后归并所有子图结果选出 Top-K 商品。

本服务支持水平扩展：通过 `docker-compose scale` 动态调整 RankSub 实例数量，Master 自动将 SKU 哈希分片到所有可用子图。

## 目录结构

```
services/rank_service_master/
├── DESIGN.md                        # 详细设计文档
├── README.md                        # 本文件
├── CMakeLists.txt                   # CMake 构建配置
├── build.sh                         # 编译脚本
├── server/
│   ├── include/
│   │   └── rank_master_server.h     # RankMasterServiceImpl 声明
│   └── src/
│       ├── main.cpp                 # 服务入口
│       └── rank_master_server.cpp   # 服务实现
└── client/
    └── rank_master_test_client.cpp  # 测试客户端
```

## 编译

```bash
cd services/rank_service_master
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 编译产物

| 二进制 | 用途 |
|--------|------|
| `rank_master_server` | 精排主图服务主程序 |
| `rank_master_test_client` | 测试客户端 |

## 使用方法

### 启动 RankServiceMaster

```bash
./rank_master_server \
  --server_port=8005 \
  --sub_worker_count=10 \
  --sub_worker_addresses=rank-sub-service:8006 \
  --top_k=100
```

参数说明：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--server_port` | int32 | 8005 | 服务监听端口 |
| `--sub_worker_count` | int32 | 10 | 子图数量 |
| `--sub_worker_addresses` | string | "127.0.0.1:8006" | 子图地址列表（逗号分隔） |
| `--top_k` | int32 | 100 | 返回前 K 个商品 |
| `--enable_timing_stats` | bool | true | 是否启用详细时延统计 |

### 使用测试客户端

```bash
./rank_master_test_client \
  --server=127.0.0.1:8005 \
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

## Docker 集成

### 启动顺序

```bash
# 1. 先启动 RankSub（10 个实例）
docker-compose up -d --scale rank-sub-service=10 rank-sub-service

# 2. 等待 RankSub 就绪
sleep 10

# 3. 启动 RankMaster
docker-compose up -d rank-master-service
```

### 动态扩缩容

```bash
# 扩容到 20 个 RankSub
bash deploy/scripts/scale-rank-sub.sh 20

# 需要重启 RankMaster 以更新 Channel 池
docker-compose up -d --force-recreate rank-master-service
```

### 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SERVER_PORT` | 8005 | 服务端口 |
| `SUB_WORKER_COUNT` | 10 | 子图数量 |
| `SUB_WORKER_ADDRESSES` | "rank-sub-service:8006" | 子图地址 |
| `RANK_SUB_HOST` | "rank-sub-service" | 子图主机名 |
| `RANK_SUB_PORT` | 8006 | 子图端口 |
| `TOP_K` | 100 | 返回前 K 个商品 |

## 工作流程

```
       Client (Proxy)
            │
            │ Rank(key, skus, payload)
            ▼
   ┌─────────────────────────────────────────────┐
   │         RankServiceMaster (:8005)            │
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
| 8005 | RankServiceMaster | BRPC | 精排主图服务端口 |
| 8006 | RankServiceSub | BRPC | 精排子图服务端口 |
