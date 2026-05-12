# LingQuickRec — 搜推广时延模拟系统

## 项目简介

本项目是一个搜推广时延模拟与通信优化验证系统，采用 BRPC 通信框架和 Protocol Buffers 序列化协议，通过多阶段流水线架构模拟推荐系统的完整调用链路。系统关注平均时延和 P99 时延两项主要指标。

### 服务发现

所有需被调用的服务通过 `discovery_client` sidecar 进程向 [Discovery](services/discovery/README.md) 注册。上游服务通过 `Discover()` RPC 获取下游 UP 实例列表，按 round-robin 选取，失败自动重试。

### 错误码体系

错误码采用 `0xMMTTCCCC` 格式：

- **MM (8bit)** — 模块代码（COMMON=0x00, PROXY=0x01, RECALL=0x03, ...）
- **TT (8bit)** — 错误类型（SUCCESS/INVALID_INPUT/SERVICE_ERROR/...）
- **CCCC (16bit)** — 具体错误码

详见 [common/DESIGN.md](common/DESIGN.md#2-错误码体系)。

### 负载均衡

上游服务通过 Discovery 获取下游服务的全部 UP 实例列表，使用 round-robin 策略选取目标实例。单次调用失败后自动重试下一个实例，连续多次失败触发熔断（10s cooldown）。

### 负载仿真

系统通过以下方式模拟真实推荐场景的负载特征：

- **时延注入**：RankSub 通过 `--scoring_delay_ms` 参数模拟不同计算开销的商品打分时延
- **数据仿真**：测试客户端可指定 SKU 数量、tensor 大小、payload 大小等参数，模拟不同规模的数据传输
- **并发仿真**：Proxy 全局线程池可配置并发度，模拟不同并发请求量下的系统行为
- **副本扩缩**：Recall、Precalc、RankMaster、RankSub 均支持多副本部署，通过 docker-compose scale 模拟集群规模变化

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
  │  Feature (8003)              │<──── Redis (6379)
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

## 服务列表

| 服务 | 端口 | Proto Service | 状态 | 依赖 |
|------|------|---------------|------|------|
| Discovery | 8100 | DiscoveryService | ✅ 已完成 | — |
| Proxy | 8080 | ProxyService | ✅ 已完成 | Discovery, Feature, Recall, Precalc, Rank |
| Recall | 8001 | RecallService | ✅ 已完成 | vLLM |
| Precalc | 8004 | PrecalcService | ✅ 已完成 | KVWorker(31502) |
| RankMaster | 8005 | RankMasterService | ✅ 已完成 | RankSub(8006) |
| RankSub | 8006 | RankSubService | ✅ 已完成 | KVWorker(31502) |
| Feature | 8003 | FeatureService | 待合入 | Redis(6379) |
| KVWorker | — | KVWorkerService | 由元戎提供服务 | — |
| vLLM | — | — | 模型服务 | Qwen3-0.6B |

## 编译命令

### 前置依赖

| 依赖 | 版本要求 | 安装参考 |
|------|----------|----------|
| CMake | >= 3.14 | `apt install cmake` |
| brpc | >= 1.4 | 基础镜像 `linquickrec/base:latest` 已内置 |
| protobuf | >= 3.0 | 同上 |
| abseil-cpp | latest | 同上 |
| gflags | latest | 同上 |

### 全量构建（推荐）

```bash
./build.sh              # Release 构建
./build.sh debug        # Debug 构建
./build.sh clean        # 清理后构建
```

### 手动构建

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 编译产物

| 二进制 | 所属服务 | 说明 |
|--------|---------|------|
| `discovery_server` | Discovery | 服务发现中心服务端 |
| `discovery_client` | Discovery | 服务发现客户端（sidecar 进程） |
| `proxy_server` | Proxy | 网关服务 |
| `proxy_test_client` | Proxy | 网关手动测试客户端 |
| `proxy_integration_test` | Proxy | 网关集成测试 |
| `recall_server` | Recall | 召回服务 |
| `recall_test_client` | Recall | 召回测试客户端 |
| `precalc_server` | Precalc | 前置计算服务 |
| `precalc_test_client` | Precalc | 前置计算测试客户端 |
| `rank_master_server` | RankMaster | 精排主图服务 |
| `rank_master_test_client` | RankMaster | 精排主图测试客户端 |
| `rank_sub_server` | RankSub | 精排子图服务 |
| `rank_sub_client` | RankSub | 精排子图测试客户端 |
| `pseudo_service` | Discovery/examples | 模拟业务服务 |
| `test_discover` | Discovery/examples | 发现功能测试 |
| `test_register` | Discovery/examples | 注册/反注册测试 |
| `test_heartbeat_cycle` | Discovery/examples | 生命周期测试 |

## 目录结构

```
LinQuickRec-yh/
├── CMakeLists.txt             # 项目级构建入口
├── README.md
├── build.sh                   # 全量编译脚本
├── .agents/                   # AI 辅助技能（brpc-cmake, git-commit 等）
├── common/                    # 公共基础库（错误码/日志/线程池）
├── deploy/
│   ├── docker/                # 各服务的容器镜像定义 + docker-compose
│   └── k8s/                   # Kubernetes 部署配置
├── docs/                      # 文档
│   ├── ports.md               # 端口配置
│   └── API.md                 # API 接口文档
├── proto/                     # 所有服务的 proto 文件
├── services/
│   ├── discovery/             # 服务发现中心
│   ├── feature/               # 特征服务（待合入）
│   ├── kv_worker/             # 元戎数据系统 Worker 启动脚本
│   ├── precalc/               # 前置计算服务
│   ├── proxy/                 # 网关服务
│   ├── rank_master/           # 精排主图服务
│   ├── rank_sub/              # 精排子图服务
│   └── recall/                # 召回服务
```

## 容器搭建

所有服务的 Dockerfile 和 docker-compose 配置统一位于 `deploy/docker/`。

### 构建并启动所有服务

```bash
docker compose -f deploy/docker/docker-compose.yml build
docker compose -f deploy/docker/docker-compose.yml up -d
```

### 单独构建某个服务

```bash
# 构建 Discovery
docker build -t linquickrec/discovery:latest \
    -f deploy/docker/discovery/Dockerfile .

# 构建 Proxy
docker build -t linquickrec/proxy:latest \
    -f deploy/docker/proxy/Dockerfile .
```

### 启动容器

```bash
# 启动 Discovery
docker run -p 8100:8100 linquickrec/discovery:latest

# 启动 Proxy（依赖 Discovery）
docker run -p 8080:8080 \
    linquickrec/proxy:latest \
    --discovery_addr="discovery:8100"
```

## 后续开发

- [x] common 公共基础库（错误码/日志/线程池）
- [x] Recall 服务端和客户端
- [x] Precalc 服务端和客户端
- [x] RankMaster + RankSub 精排服务
- [x] Proxy 网关服务
- [x] Discovery 服务发现中心
- [x] API 接口文档
- [ ] Feature 服务端和客户端（待其他开发者合入）
- [ ] 集成 Redis 进行特征存储
- [ ] 实现 KVWorker 内存管理
- [ ] 实现轻量级探针和数据采集
- [ ] 构建监控可视化界面
- [ ] 添加健康检查
- [ ] 添加自动扩缩容支持
