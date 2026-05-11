# LingQuickRec - 搜推广时延模拟系统

## 项目简介

本项目是一个用于模拟搜推广过程时延并验证通信优化效果的实验系统。系统关注平均时延和 P99 时延两项主要指标。

## 系统架构

```
用户请求
    ↓
┌─────────────────┐
│  Proxy 服务     │
│  (网关)         │
└─────────────────┘
    ↓
┌─────────────────┐
│  Feature 服务   │←──→ Redis (6379)
│  (特征服务)     │
│  端口：8003     │
└─────────────────┘
    ↓
┌─────────────────┬─────────────────┬─────────────────┐
│  Recall 服务    │  Precalc 服务   │  Rank 服务      │
│  (召回服务)     │  (前置计算)     │  (精排服务)     │
│  端口：8001     │  端口：8004     │  Master:8005    │
│  RecallKVWorker │  RankKVWorker   │  Sub:8006       │
│  端口：31501    │  端口：31502    │  RankKVWorker   │
│  (远程)         │  (远程)         │  端口：31502    │
└─────────────────┴─────────────────┴─────────────────┘
       ↓                  ↓                  ↓
    ┌─────────────────┐         ┌─────────────────┐
    │ RecallKVWorker  │         │  RankKVWorker   │
    │ (元戎 KVCache)  │         │ (元戎 KVCache)  │
    │ 141.61.84.245   │         │ 141.61.84.245   │
    │ 不依赖 Redis    │         │ 不依赖 Redis    │
    └─────────────────┘         └─────────────────┘
```

## 服务列表

| 服务名            | 端口   | Proto Service 名 | 状态    | 依赖                                                     |
| -------------- | ---- | --------------- | ----- | ------------------------------------------------------ |
| Proxy          | -    | Proxy           | 规划中   | feature(8003), recall(8001), precalc(8004), rank(8005) |
| FeatureService | 8003 | FeatureService  | 规划中   | redis(6379)                                            |
| RecallService  | 8001 | RecallService   | ✅ 已完成 | recall\_kvworker(31501)                                |
| PrecalcService | 8004 | PrecalcService  | 进行中   | rank\_kvworker(31502)                                  |
| RankMaster     | 8005 | RankMasterService | ✅ 已完成 | rank\_kvworker(31502), precalc(8004)                   |
| RankSub        | 8006 | RankSubService  | ✅ 已完成 | rank\_kvworker(31502)                                  |
| RecallKVWorker | 31501 | KVWorkerService | 元戎提供 (远程) | 无                                                      |
| RankKVWorker   | 31502 | KVWorkerService | 元戎提供 (远程) | 无                                                      |
| Redis          | 6379 | -               | 基础设施  | -                                                      |
| vLLM           | 8000 | -               | 模型服务  | -                                                      |

## 目录结构

```
Vllm-brpc-Gateway/
├── docker-compose.yml          # 容器编排配置
├── Dockerfile.base             # 基础镜像
├── README.md
├── proto/                      # 所有服务的 proto 文件
│   ├── proxy.proto             # 网关服务（Proxy）
│   ├── feature.proto           # 特征服务（FeatureService）
│   ├── recall.proto            # 召回服务（RecallService）
│   ├── precalc.proto           # 前置计算服务（PrecalcService）
│   └── rank.proto              # 精排服务（RankService）
├── services/                   # 5 个业务服务（文件夹名与 proto 中 service 名对应）
│   ├── Proxy/                  # 网关服务
│   │   ├── CMakeLists.txt
│   │   └── Dockerfile
│   ├── FeatureService/         # 特征服务
│   │   ├── CMakeLists.txt
│   │   └── Dockerfile
│   ├── RankService/            # 精排服务
│   │   ├── CMakeLists.txt
│   │   └── Dockerfile
│   ├── PrecalcService/         # 前置计算服务
│   │   ├── CMakeLists.txt
│   │   └── Dockerfile
│   └── recall/                 # 召回服务（已实现）
│       ├── server/
│       │   └── recall_server.cpp       # 服务端代码
│       ├── client/
│       │   └── recall_test_client.cpp  # 客户端测试代码
│       ├── include/                    # 工具头文件（线程池等）
│       ├── backup/                     # 备份代码
│       ├── CMakeLists.txt
│       └── Dockerfile
├── kvworker/                   # KVWorker 服务（元戎）
│   ├── recall_kvworker/        # Recall 专用 KVWorker
│   │   ├── Dockerfile
│   │   ├── CMakeLists.txt
│   │   └── config.yaml
│   └── rank_kvworker/          # Rank 专用 KVWorker
│       ├── Dockerfile
│       ├── CMakeLists.txt
│       └── config.yaml
├── docs/                       # 文档目录
│   ├── API.md                  # API 接口文档
│   └── ports.md                # 端口配置文档
├── redis/
│   └── redis.conf
└── vllm/                       # vLLM 模型服务
    └── Dockerfile
```

## 快速开始

### 1. 构建基础镜像

```bash
docker build -t lingquickrec/base:latest -f Dockerfile.base .
```

### 2. 构建并启动所有服务

```bash
docker-compose up --build
```

### 3. 单独启动某个服务

```bash
# 只启动网关服务
docker-compose up gateway

# 只启动召回服务
docker-compose up recall
```

### 4. 查看日志

```bash
# 查看所有服务日志
docker-compose logs -f

# 查看特定服务日志
docker-compose logs -f gateway
```

### 5. 停止服务

```bash
# 停止所有服务
docker-compose down

# 停止并删除数据卷
docker-compose down -v
```

## 服务间通信

所有服务通过 Docker 网络直接通信，使用容器名作为主机名：

```cpp
// 示例：网关服务访问特征服务
std::string feature_host = "feature";  // 容器名
int feature_port = 8003;

// 示例：Precalc 服务访问 RecallKVWorker
std::string kvworker_host = "recall_kvworker";  // 容器名
int kvworker_port = 8002;
```

## 环境变量配置

每个服务可以通过环境变量配置依赖服务的地址：

```yaml
environment:
  - FEATURE_HOST=feature
  - RECALL_HOST=recall
  - RANKING_HOST=ranking
  - PRECOMPUTE_HOST=precompute
  - KVWORKER_HOST=kvworker
  - REDIS_HOST=redis
```

## 监控指标

- **端到端时延**：网关服务统计（平均、P99）
- **服务间通信时延**：每个 BRPC 客户端统计
- **计算时延**：每个服务端统计
- **trace\_id 全链路追踪**：所有服务传递 trace\_id

## 开发指南

### 添加新服务

1. 在 `services/` 下创建新目录
2. 创建 `Dockerfile`、`CMakeLists.txt`、`proto/`、`src/`
3. 在 `docker-compose.yml` 中添加服务配置
4. 在 `proto/` 中添加 proto 文件

### 修改现有服务

1. 修改对应服务的源代码
2. 重新构建：`docker-compose up --build <service_name>`

### 调试单个服务

```bash
# 进入容器
docker-compose exec gateway /bin/bash

# 查看服务状态
docker-compose ps
```

## 技术栈

- **容器化**: Docker + Docker Compose
- **通信框架**: BRPC
- **序列化**: Protocol Buffers
- **缓存**: Redis
- **AI 框架**: 元戎 (openYuanrong)
- **模型**: Qwen3-0.6B
- **JSON 处理**: RapidJSON

## 注意事项

1. **GPU 支持**: RecallKVWorker 和 RankKVWorker 需要 GPU 支持，确保安装了 NVIDIA Docker
2. **网络配置**: 所有容器在同一 Docker 网络中，通过容器名通信
3. **数据持久化**: Redis 和 KVWorker 的数据可以通过 volume 持久化
4. **资源限制**: 可以在 docker-compose.yml 中为每个容器设置资源限制
5. **KVWorker 独立性**: RecallKVWorker 和 RankKVWorker 是两个独立的容器，都不依赖 Redis

## 后续开发

- [x] RecallService 服务端和客户端实现（已完成）
- [x] API 接口文档创建（已完成）
- [ ] PrecalcService 服务端和客户端实现（进行中）
- [ ] ProxyService 服务端和客户端实现
- [ ] FeatureService 服务端和客户端实现
- [ ] RankService 服务端和客户端实现
- [ ] 集成 Redis 进行特征存储
- [ ] 实现 KVWorker 内存管理
- [ ] 集成 Qwen3-0.6B 模型推理
- [ ] 实现轻量级探针和数据采集
- [ ] 构建监控可视化界面
- [ ] 添加健康检查
- [ ] 添加自动扩缩容支持

