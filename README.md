# LingQuickRec — 搜推广时延模拟系统

## 项目简介

本项目是一个搜推广时延模拟与通信优化验证系统，采用 BRPC 通信框架和 Protocol Buffers 序列化协议，通过多阶段流水线架构模拟推荐系统的完整调用链路。系统关注平均时延和 P99 时延两项主要指标。

各服务通过 [discovery](services/discovery/README.md) 服务发现中心实现动态注册与实例发现，无需静态配置下游地址。

## 系统架构

```
          External Request
                 |
                 v
  ┌──────────────────────────────┐
  │  Proxy (8080)                │<──── Discovery (8100)
  └──────────────┬───────────────┘
                 |
                 v
  ┌──────────────────────────────┐
  │  FeatureService (8003)       │<──── Redis (6379)
  │  [pending]                   │
  └──────────────┬───────────────┘
                 |
                 v
     (parallel, global thread pool)
                 |
        ┌────────┼────────┐
        v                 v
  ┌────────────┐   ┌──────────────┐
  │ Recall xN  │   │ Precalc xN   │
  │ 8001       │   │ 8004         │
  └──────┬─────┘   └──────┬───────┘
         │ Write          │ Write
         v                v
  ┌────────────┐   ┌──────────────┐
  │ KVWorker   │   │ KVWorker     │
  │ 31501      │   │ 31502        │
  └────────────┘   └──────┬───────┘
                          │ Read
                          v
                   ┌──────────────┐
                   │ RankMaster   │
                   │ xN, 8005     │
                   └──────┬───────┘
                          │
                          v
                   ┌──────────────┐
                   │ RankSub xN   │
                   │ 8006         │
                   └──────┬───────┘
                          │ Read
                          v
                   ┌──────────────┐
                   │ KVWorker     │
                   │ 31502        │
                   └──────────────┘
```

## 服务列表

| 服务 | 端口 | Proto Service | 状态 | 依赖 |
|------|------|---------------|------|------|
| discovery-server | 8100 | DiscoveryService | ✅ 已完成 | — |
| Proxy | 8080 | ProxyService | ✅ 已完成 | discovery, feature, recall, precalc, rank |
| RecallService | 8001 | RecallService | ✅ 已完成 | vLLM(8000) |
| PrecalcService | 8004 | PrecalcService | ✅ 已完成 | KVWorker(31502) |
| RankMaster | 8005 | RankMasterService | ✅ 已完成 | RankSub(8006) |
| RankSub | 8006 | RankSubService | ✅ 已完成 | KVWorker(31502) |
| FeatureService | 8003 | FeatureService | 待合入 | Redis(6379) |
| kv_worker | — | KVWorkerService | 元戎提供(远程) | — |
| vLLM | 8000 | — | 模型服务 | — |

## 目录结构

```
LinQuickRec-yh/
├── CMakeLists.txt             # 项目级构建入口
├── README.md
├── proto/                     # 所有服务的 proto 文件
│   ├── proxy.proto
│   ├── feature.proto
│   ├── recall.proto
│   ├── precalc.proto
│   ├── rank_master.proto
│   ├── rank_sub.proto
│   └── discovery.proto
├── common/                    # 公共基础库
│   ├── include/common/        # 对外头文件
│   ├── src/                   # 实现
│   ├── tests/                 # 单元测试
│   └── examples/              # 使用示例
├── services/
│   ├── discovery/             # 服务发现中心
│   │   ├── server/            # discovery_server
│   │   ├── client/            # discovery_client
│   │   └── examples/          # docker-compose 端到端演示
│   ├── proxy/                 # 网关服务
│   │   ├── server/
│   │   ├── client/
│   │   └── tests/
│   ├── recall/                # 召回服务
│   │   ├── server/
│   │   ├── client/
│   │   └── tests/
│   ├── precalc/               # 前置计算服务
│   │   ├── server/
│   │   ├── client/
│   │   └── tests/
│   ├── rank_master/           # 精排主图服务
│   │   ├── server/
│   │   ├── client/
│   │   └── tests/
│   ├── rank_sub/              # 精排子图服务
│   │   ├── server/
│   │   ├── client/
│   │   └── tests/
│   ├── feature/               # 特征服务（待合入）
│   │   ├── CMakeLists.txt
│   │   └── Dockerfile
│   └── kv_worker/             # 元戎数据系统 Worker 启动脚本
├── deploy/
│   ├── docker/                # 各服务的容器镜像定义
│   │   ├── discovery/         # discovery-server Dockerfile + entrypoint
│   │   ├── proxy/
│   │   ├── recall/
│   │   ├── precalc/
│   │   ├── rank-master/
│   │   ├── rank-sub/
│   │   ├── feature_service/
│   │   ├── kv_worker/
│   │   └── examples/          # 端到端演示容器
│   └── k8s/                   # Kubernetes 部署配置
├── docs/                      # 文档
│   ├── ports.md               # 端口配置
│   └── API.md                 # API 接口文档
└── .agents/
    └── skills/                # AI 辅助技能（brpc-cmake, git-commit 等）
```

## 编译命令

### 前置依赖

| 依赖 | 版本要求 | 安装参考 |
|------|----------|----------|
| CMake | >= 3.14 | `apt install cmake` |
| brpc | >= 1.4 | 基础镜像 `linquickrec/base:latest` 已内置 |
| protobuf | >= 3.0 | 同上 |
| abseil-cpp | latest | 同上 |
| gflags | latest | 同上 |

### 从项目根目录构建

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make discovery_server discovery_client -j$(nproc)
```

### 从服务目录单独构建

每个服务目录下提供 `build.sh` 脚本，支持参数：

```bash
cd services/<service_name>
./build.sh                   # 默认 Release 构建
./build.sh clean             # 清理后构建
./build.sh debug             # Debug 构建
./build.sh release           # Release 构建
```

产物统一输出到 `build/bin/` 目录。

### 编译产物

| 二进制 | 所属服务 | 说明 |
|--------|---------|------|
| `discovery_server` | discovery | 服务发现中心服务端 |
| `discovery_client` | discovery | 服务发现客户端（sidecar 进程） |
| `proxy_server` | proxy | 网关服务 |
| `proxy_test_client` | proxy | 网关手动测试客户端 |
| `proxy_integration_test` | proxy | 网关集成测试 |
| `recall_server` | recall | 召回服务 |
| `recall_test_client` | recall | 召回测试客户端 |
| `precalc_server` | precalc | 前置计算服务 |
| `precalc_test_client` | precalc | 前置计算测试客户端 |
| `rank_master_server` | rank_master | 精排主图服务 |
| `rank_master_test_client` | rank_master | 精排主图测试客户端 |
| `rank_sub_server` | rank_sub | 精排子图服务 |
| `rank_sub_client` | rank_sub | 精排子图测试客户端 |
| `pseudo_service` | discovery/examples | 模拟业务服务 |
| `test_discover` | discovery/examples | 发现功能测试 |
| `test_register` | discovery/examples | 注册/反注册测试 |
| `test_heartbeat_cycle` | discovery/examples | 生命周期测试 |

## 容器搭建

所有服务的 Dockerfile 统一位于 `deploy/docker/<service>/`，通过 entrypoint.sh 启动。

### 构建镜像

```bash
# 构建 discovery-server
docker build -t linquickrec/discovery:latest \
    -f deploy/docker/discovery/Dockerfile .

# 构建 proxy
docker build -t linquickrec/proxy:latest \
    -f deploy/docker/proxy/Dockerfile .
```

### 启动容器

```bash
# 启动 discovery-server
docker run -p 8100:8100 linquickrec/discovery:latest

# 启动 proxy（依赖 discovery-server）
docker run -p 8080:8080 \
    linquickrec/proxy:latest \
    --discovery_addr="discovery-server:8100"
```

### 端到端演示

参见 [discovery/examples/README.md](services/discovery/examples/README.md)，提供完整的 docker-compose 编排，启动 9 个容器演示完整的注册/发现/心跳链路。

## 服务发现

所有需被调用的服务通过 `discovery_client` sidecar 进程向 `discovery-server` 注册。上游服务通过 `Disover()` RPC 获取下游 UP 实例列表，按 round-robin 选取，失败自动重试。详见 [discovery/README.md](services/discovery/README.md)。

## 错误码体系

错误码采用 `0xMMTTCCCC` 格式：

- **MM (8bit)** — 模块代码（COMMON=0x00, PROXY=0x01, RECALL=0x03, ...）
- **TT (8bit)** — 错误类型（SUCCESS/INVALID_INPUT/SERVICE_ERROR/...）
- **CCCC (16bit)** — 具体错误码

详见 [common/DESIGN.md](common/DESIGN.md#2-错误码体系) 和 [common/include/common/internal/error/error_code.h](common/include/common/internal/error/error_code.h)。

## 测试方法

### 集成测试

Proxy 提供单进程集成测试，零外部依赖：

```bash
cd services/proxy
./build.sh
./build/bin/proxy_integration_test
```

### 手动测试

使用各服务的 `*_test_client` 二进制进行手动验证。

## 技术栈

| 类别 | 技术 |
|------|------|
| 容器化 | Docker + Docker Compose |
| 通信框架 | BRPC |
| 序列化 | Protocol Buffers |
| 服务发现 | BRPC RPC（自研） |
| 日志 | common::logger（自研，流式 + 格式化） |
| 线程池 | common::ThreadPool（自研，全局单例） |
| 错误码 | common::error::Status（自研，0xMMTTCCCC） |
| 模型推理 | vLLM (Qwen3-0.6B) |
| JSON 处理 | RapidJSON |
| AI 框架 | 元戎 (openYuanrong) |
| 部署 | Kubernetes (deploy/k8s/) |

## 后续开发

- [x] common 公共基础库（错误码/日志/线程池）
- [x] RecallService 服务端和客户端
- [x] PrecalcService 服务端和客户端
- [x] RankServiceMaster + RankServiceSub 精排服务
- [x] Proxy 网关服务
- [x] Discovery 服务发现中心
- [x] API 接口文档
- [ ] FeatureService 服务端和客户端（待其他开发者合入）
- [ ] 集成 Redis 进行特征存储
- [ ] 实现 KVWorker 内存管理
- [ ] 实现轻量级探针和数据采集
- [ ] 构建监控可视化界面
- [ ] 添加健康检查
- [ ] 添加自动扩缩容支持
