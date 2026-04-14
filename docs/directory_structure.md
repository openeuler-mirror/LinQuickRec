# 容器化目录结构设计说明

## ✅ 设计原则

### 1. 服务独立性
每个服务（Gateway、Feature、Recall、Ranking、Precompute）都是独立的构建和部署单元：
- 独立的 `Dockerfile` - 可单独构建镜像
- 独立的 `CMakeLists.txt` - 可单独编译
- 独立的 `proto/` 目录 - 服务间解耦
- 独立的 `client/` 和 `server/` - 职责清晰

### 2. 您的代码完整保留
**重要**: 您的召回服务代码（`brpc_client.cpp`, `brpc_server.cpp`, `CMakeLists.txt`）完整保留在 `services/recall/` 目录中，没有任何改动。

### 3. 容器化友好
- **docker-compose.yml**: 统一管理 8 个容器
- **Dockerfile.base**: 共享基础镜像，减少重复
- **容器名通信**: 直接通过容器名（如 `feature`, `recall`）进行网络通信
- **环境变量**: 通过环境变量配置服务依赖

### 4. Proto 文件管理
- **根目录 `proto/`**: 存放所有服务的 proto 文件，便于全局查看
- **服务内 `proto/`**: 每个服务有自己的 proto 目录（构建时可 symlink 或复制）
- **KVWorker proto**: 独立在 `kvworker/proto/` 中

### 5. 基础设施分离
- **KVWorker**: 独立容器，搭载元戎，需要 GPU 支持
- **Redis**: 独立容器，配置在 `redis/redis.conf`
- **业务服务**: 5 个无状态服务，可水平扩展

## 📁 目录结构详解

```
Vllm-brpc-Gateway/
│
├── docker-compose.yml          # ⭐ 核心：容器编排配置
├── Dockerfile.base             # ⭐ 基础镜像（BRPC + 元戎 + 依赖）
├── README.md                   # 项目说明
├── CMakeLists.txt              # 根构建配置（可选）
│
├── proto/                      # ⭐ 所有 proto 文件（全局视图）
│   ├── gateway.proto           # 网关服务协议
│   ├── feature.proto           # 特征服务协议
│   ├── recall.proto            # 召回服务协议
│   ├── ranking.proto           # 排序服务协议
│   ├── precompute.proto        # 前置计算服务协议
│   └── recommend.proto         # 您的原始 proto（保留）
│
├── services/                   # ⭐ 5 个业务服务
│   ├── gateway/                # 网关服务
│   │   ├── Dockerfile          # 网关镜像构建
│   │   ├── CMakeLists.txt      # 网关编译配置
│   │   ├── proto/              # 网关 proto（可 symlink 到根 proto）
│   │   ├── client/             # 网关调用其他服务的客户端
│   │   └── server/             # 网关服务端
│   │
│   ├── feature/                # 特征服务
│   │   ├── Dockerfile
│   │   ├── CMakeLists.txt
│   │   ├── proto/
│   │   ├── client/
│   │   └── server/
│   │
│   ├── recall/                 # ⭐ 召回服务（您的代码在这里）
│   │   ├── Dockerfile          # 已创建
│   │   ├── CMakeLists.txt      # 您的原始文件（保留）
│   │   ├── client/             # 您的 brpc_client.cpp（保留）
│   │   ├── server/             # 您的 brpc_server.cpp（保留）
│   │   └── proto/              # 可放入 recommend.proto 或 recall.proto
│   │
│   ├── ranking/                # 排序服务
│   │   ├── Dockerfile
│   │   ├── CMakeLists.txt
│   │   ├── proto/
│   │   ├── client/
│   │   └── server/
│   │
│   └── precompute/             # 前置计算服务
│       ├── Dockerfile
│       ├── CMakeLists.txt
│       ├── proto/
│       ├── client/
│       └── server/
│
├── kvworker/                   # ⭐ KVWorker 服务（元戎）
│   ├── Dockerfile              # KVWorker 镜像构建
│   ├── CMakeLists.txt          # KVWorker 编译配置
│   ├── config.yaml             # KVWorker 配置（blocks、内存等）
│   ├── proto/
│   │   └── kvworker.proto      # KVWorker 服务协议
│   └── src/                    # KVWorker 源码
│       ├── kvworker_server.cpp
│       └── memory_manager.cpp
│
├── redis/                      # ⭐ Redis 配置
│   └── redis.conf              # Redis 配置文件
│
├── common/                     # 公共代码（可选）
├── monitoring/                 # 监控探针代码（可选）
├── scripts/                    # ⭐ 辅助脚本
│   ├── build_all.sh            # 构建所有服务
│   ├── start_all.sh            # 启动所有服务
│   └── stop_all.sh             # 停止所有服务
│
└── docs/                       # 文档
    ├── architecture.md         # 系统架构文档
    └── deployment.md           # 部署文档
```

## 🎯 关键设计决策

### 1. 为什么每个服务要有独立的 proto 目录？
**原因**: 
- 服务间解耦，每个服务只依赖自己的 proto
- 构建时只复制需要的 proto 文件，减小镜像体积
- 便于版本管理和服务演进

**实现方式**:
```dockerfile
# 方式 1: 复制根目录 proto
COPY proto/recall.proto /app/proto/

# 方式 2: 使用服务内 proto（推荐）
COPY services/recall/proto/ /app/proto/
```

### 2. 为什么 KVWorker 独立在根目录？
**原因**:
- KVWorker 不是业务服务，而是**共享基础设施**
- 搭载元戎，需要特殊的 GPU 配置
- Recall 和 Ranking 都依赖它，地位类似 Redis

### 3. 如何保留您的召回服务代码？
**完全保留**:
- `services/recall/CMakeLists.txt` - 您的原始文件
- `services/recall/client/brpc_client.cpp` - 您的客户端代码
- `services/recall/server/brpc_server.cpp` - 您的服务端代码
- `proto/recommend.proto` - 您的原始 proto 文件

**新增内容**:
- `services/recall/Dockerfile` - 容器化配置
- `proto/recall.proto` - 新的 proto（可选替换 recommend.proto）

### 4. 容器间如何通信？
**通过 Docker 网络和容器名**:
```yaml
# docker-compose.yml
services:
  gateway:
    environment:
      - RECALL_HOST=recall  # 容器名
      - RECALL_PORT=8001
  
  recall:
    # 容器名就是 'recall'
```

```cpp
// C++ 代码中使用
std::string recall_host = getenv("RECALL_HOST");  // "recall"
int recall_port = 8001;

// 或者直接使用容器名
std::string recall_host = "recall";
```

## 🚀 使用流程

### 1. 开发阶段
```bash
# 在本地修改代码
# 例如：修改 services/recall/server/brpc_server.cpp

# 重新构建召回服务
docker-compose up --build recall

# 查看日志
docker-compose logs -f recall
```

### 2. 测试阶段
```bash
# 启动所有服务
docker-compose up -d

# 测试召回服务
curl http://localhost:8001/generate -d '{...}'

# 查看服务状态
docker-compose ps
```

### 3. 部署阶段
```bash
# 构建所有服务
./scripts/build_all.sh

# 启动所有服务
./scripts/start_all.sh

# 停止所有服务
./scripts/stop_all.sh
```

## ✅ 优势总结

### 对比之前的结构：

| 特性 | 之前 | 现在 |
|------|------|------|
| 服务隔离 | ❌ 所有代码混在一起 | ✅ 每个服务独立目录 |
| 容器化支持 | ❌ 需要手动配置 | ✅ Dockerfile + docker-compose |
| 服务通信 | ❌ 需要配置 IP | ✅ 直接使用容器名 |
| 代码保留 | - | ✅ 您的召回代码完整保留 |
| 扩展性 | ❌ 添加新服务困难 | ✅ 复制模板即可 |
| 文档 | ❌ 缺少架构文档 | ✅ 完整的架构和使用文档 |

## 📝 下一步工作

1. **实现其他服务**: 参照 recall 服务的结构，实现 gateway、feature、ranking、precompute
2. **完善 KVWorker**: 实现 KVCache 内存管理和元戎集成
3. **添加监控**: 在 `monitoring/` 中实现轻量级探针
4. **测试容器化**: 使用 docker-compose 测试服务间通信
5. **性能调优**: 根据时延分析结果优化通信和计算

## 🎉 总结

这个目录结构是**容器化友好**的，完全符合您的需求：
- ✅ 每个服务独立成容器
- ✅ KVWorker 和 Redis 作为独立容器
- ✅ 通过容器名直接通信
- ✅ 您的召回服务代码完整保留
- ✅ 便于未来扩展和维护

如果您有任何调整需求，随时告诉我！
