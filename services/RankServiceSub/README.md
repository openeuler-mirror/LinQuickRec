# 精排子图服务 — RankServiceSub

## 模块简介

RankServiceSub 是推荐系统精排层的工作节点，负责从元戎 KVWorker 读取前置计算结果（用户特征 tensor），对分配到的候选 SKU 进行打分。本服务采用无状态设计，支持通过 `docker-compose scale` 水平扩展。

多个 RankSub 实例共享同一 Docker 网络服务名 `rank-sub-service`，RankServiceMaster 通过 Docker 内部 DNS 自动负载均衡到不同实例。

## 目录结构

```
services/RankServiceSub/
├── DESIGN.md                    # 详细设计文档
├── README.md                    # 本文件
├── CMakeLists.txt               # CMake 构建配置
├── build.sh                     # 编译脚本
├── server/
│   ├── include/
│   │   └── rank_sub_server.h    # RankSubServiceImpl 声明
│   └── src/
│       ├── main.cpp             # 服务入口
│       └── rank_sub_server.cpp  # 服务实现
└── client/
    └── rank_sub_client.cpp      # 测试客户端
```

## 编译

```bash
cd services/RankServiceSub
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 编译产物

| 二进制 | 用途 |
|--------|------|
| `rank_sub_server` | 精排子图服务主程序 |
| `rank_sub_client` | 测试客户端 |

## 使用方法

### 启动 RankServiceSub

```bash
./rank_sub_server \
  --server_port=8006 \
  --kvworker_host=141.61.84.245 \
  --kvworker_port=31502 \
  --scoring_delay_ms=100
```

参数说明：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--server_port` | int32 | 8006 | 服务监听端口 |
| `--kvworker_host` | string | "141.61.84.245" | KVWorker 主机地址 |
| `--kvworker_port` | int32 | 31502 | KVWorker 端口 |
| `--etcd_address` | string | "141.61.84.245:2379" | ETCD 地址 |
| `--scoring_delay_ms` | int32 | 100 | 模拟打分耗时（毫秒） |
| `--enable_timing_stats` | bool | true | 是否启用详细时延统计 |

### 使用测试客户端

```bash
./rank_sub_client --server=127.0.0.1:8006
```

## Docker 集成

### 启动多个实例

```bash
# 启动 10 个 RankSub 实例
docker-compose up -d --scale rank-sub-service=10

# 扩容到 20 个
docker-compose up -d --scale rank-sub-service=20

# 缩容到 5 个
docker-compose up -d --scale rank-sub-service=5
```

### 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SERVER_PORT` | 8006 | 服务端口 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 主机 |
| `KVWORKER_PORT` | 31502 | KVWorker 端口 |
| `SCORING_DELAY_MS` | 100 | 模拟打分延迟 |

### 注意事项

- **不设 container_name**：scale 时多个容器不能同名
- **端口范围映射**：`8006-8015:8006`（宿主机访问用）
- **同一网络**：所有实例加入 `lingquickrec` 网络

## 工作流程

```
       RankServiceMaster
            │
            │ Rank(key, skus_sub, payload)
            ▼
   ┌─────────────────────┐         ┌──────────────────┐
   │  RankSubService     │  Read   │  KVWorker         │
   │  (:8006)            │────────▶│  (:31502)         │
   │                     │         │                   │
   │  1. Read user_feat  │◀────────│  8.5MB tensor     │
   │  2. Parse SKUs      │         │                   │
   │  3. Score each SKU  │         └──────────────────┘
   │  4. Return scores   │
   └─────────────────────┘
```

## 水平扩展架构

```
                    Docker Network
                    ┌───────────────────────────────────┐
                    │                                   │
   ┌────────────────┼───────────────────────────────────┼──────┐
   │                │          DNS Round Robin          │      │
   │                ▼                                   ▼      │
   │  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐     │
   │  │ RankSub #0   │ │ RankSub #1   │ │ RankSub #N   │     │
   │  │ :8006        │ │ :8006        │ │ :8006        │     │
   │  └──────────────┘ └──────────────┘ └──────────────┘     │
   │                     ▲                                   │
   │                     │ rank-sub-service:8006             │
   │           ┌─────────┴──────────┐                       │
   │           │  RankMaster (:8005)│                       │
   │           └────────────────────┘                       │
   └──────────────────────────────────────────────────────────┘
```

## 端口对照表

| 端口 | 服务 | 协议 | 说明 |
|------|------|------|------|
| 8006 | RankServiceSub | BRPC | 精排子图服务端口（所有实例统一） |
| 31502 | KVWorker (Rank) | 元戎 SDK | 分布式缓存端口 |
