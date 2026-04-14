# LingQuickRec - 搜推广时延模拟系统（容器化版本）

## 项目简介

本项目是一个用于模拟搜推广过程时延并验证通信优化效果的实验系统。系统采用容器化部署，包含 8 个独立容器，关注平均时延和 P99 时延两项主要指标。

## 系统架构

```
用户请求 (8080)
    ↓
┌─────────────────┐
│  Gateway 容器   │
│  (网关服务)     │
└─────────────────┘
    ↓
┌─────────────────┐
│  Feature 容器   │←──→ Redis 容器 (6379)
│  (特征服务)     │
└─────────────────┘
    ↓
┌─────────────────┬─────────────────┐
│  Recall 容器    │  Precompute 容器│
│  (召回服务)     │  (前置计算)     │
│  Qwen-0.6B      │  8MB tensor     │
│  KVWorker1      │  KVWorker2      │
└─────────────────┴─────────────────┘
              ↓
         ┌─────────────────┐
         │  KVWorker 容器   │
         │  (元戎 + KVCache)│
         │  8002           │
         └─────────────────┘
              ↓
┌─────────────────┐
│  Ranking 容器   │
│  (排序服务)     │
└─────────────────┘
    ↓
返回结果
```

## 容器列表

| 容器名 | 服务 | 端口 | 依赖 |
|--------|------|------|------|
| gateway | 网关服务 | 8080 | feature, recall, ranking, precompute |
| feature | 特征服务 | 8003 | redis |
| recall | 召回服务 | 8001 | kvworker |
| ranking | 排序服务 | 8005 | kvworker, recall, precompute |
| precompute | 前置计算 | 8004 | kvworker |
| kvworker | KVWorker(元戎) | 8002 | redis |
| redis | Redis 缓存 | 6379 | - |

## 目录结构

```
Vllm-brpc-Gateway/
├── docker-compose.yml          # 容器编排配置
├── Dockerfile.base             # 基础镜像
├── README.md
├── proto/                      # 所有服务的 proto 文件
│   ├── gateway.proto
│   ├── feature.proto
│   ├── recall.proto
│   ├── ranking.proto
│   └── precompute.proto
├── services/                   # 5 个业务服务
│   ├── gateway/
│   │   ├── Dockerfile
│   │   ├── CMakeLists.txt
│   │   ├── proto/
│   │   └── src/
│   ├── feature/
│   │   ├── Dockerfile
│   │   ├── CMakeLists.txt
│   │   ├── proto/
│   │   └── src/
│   ├── recall/
│   │   ├── Dockerfile
│   │   ├── CMakeLists.txt
│   │   ├── client/
│   │   │   └── brpc_client.cpp
│   │   ├── server/
│   │   │   └── brpc_server.cpp
│   │   └── proto/
│   │       └── recommend.proto
│   ├── ranking/
│   │   ├── Dockerfile
│   │   ├── CMakeLists.txt
│   │   ├── proto/
│   │   └── src/
│   └── precompute/
│       ├── Dockerfile
│       ├── CMakeLists.txt
│       ├── proto/
│       └── src/
├── kvworker/                   # KVWorker 服务（元戎）
│   ├── Dockerfile
│   ├── CMakeLists.txt
│   ├── config.yaml
│   ├── proto/
│   │   └── kvworker.proto
│   └── src/
└── redis/
    └── redis.conf
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
- **trace_id 全链路追踪**：所有服务传递 trace_id

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
- **AI 框架**: 元戎 (OneDNN)
- **模型**: Qwen-0.6B

## 注意事项

1. **GPU 支持**: KVWorker 容器需要 GPU 支持，确保安装了 NVIDIA Docker
2. **网络配置**: 所有容器在同一 Docker 网络中，通过容器名通信
3. **数据持久化**: Redis 和 KVWorker 的数据可以通过 volume 持久化
4. **资源限制**: 可以在 docker-compose.yml 中为每个容器设置资源限制

## 后续开发

- [ ] 实现各服务的 BRPC 客户端/服务端
- [ ] 集成 Redis 进行特征存储
- [ ] 实现 KVWorker 内存管理
- [ ] 集成 Qwen-0.6B 模型推理
- [ ] 实现轻量级探针和数据采集
- [ ] 构建监控可视化界面
- [ ] 添加健康检查
- [ ] 添加自动扩缩容支持
