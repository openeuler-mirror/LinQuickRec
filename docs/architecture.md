# LingQuickRec 系统架构文档

## 1. 系统概述

### 1.1 设计目标
- 模拟搜推广过程的每一步时延
- 验证通信优化效果
- 关注平均时延和 P99 时延
- 低 QPS 场景（不同于京东阿里的高并发场景）

### 1.2 核心组件
- **5 个业务服务**: Gateway、Feature、Recall、Ranking、Precompute
- **2 个基础设施**: KVWorker（元戎）、Redis
- **通信方式**: BRPC + 容器名直接通信

## 2. 服务架构

### 2.1 Gateway 服务（网关）
**端口**: 8080  
**职责**:
- 接收用户推荐请求
- 生成唯一 trace_id
- 协调各服务调用（串行 + 并行）
- 返回最终推荐结果
- 统计端到端时延

**调用流程**:
```
1. 接收用户请求
2. 调用 Feature 服务（获取用户特征）
3. 并行调用:
   - Recall 服务（召回推理）
   - Precompute 服务（用户特征编码）
4. 调用 Feature 服务（获取商品信息）
5. 调用 Ranking 服务（排序）
6. 返回结果
```

### 2.2 Feature 服务（特征）
**端口**: 8003  
**职责**:
- 从 Redis 读取用户特征
- 计算缺失的用户特征
- 将计算结果写入 Redis
- 提供商品信息查询

**依赖**: Redis 容器

### 2.3 Recall 服务（召回）
**端口**: 8001  
**职责**:
- 基于 Qwen-0.6B 的生成式召回
- 调用 KVWorker 读取/写入 KVCache
- 返回召回商品列表

**技术细节**:
- 模型：Qwen-0.6B
- KVCache 配置：16 blocks/user, 768KB/block, 12MB/user
- 依赖：KVWorker 容器

### 2.4 Precompute 服务（前置计算）
**端口**: 8004  
**职责**:
- 用户级特征编码
- 生成 8MB tensor
- 写入 KVWorker2

**依赖**: KVWorker 容器

### 2.5 Ranking 服务（排序）
**端口**: 8005  
**职责**:
- 从 KVWorker2 读取用户特征编码
- 融合召回结果、用户特征、商品特征
- 生成式排序推理
- 返回排序结果

**依赖**: KVWorker、Recall、Precompute

### 2.6 KVWorker 服务
**端口**: 8002  
**职责**:
- KVCache 内存管理
- 提供读取/写入接口
- 搭载元戎（OneDNN）进行加速

**技术细节**:
- 内存分配：16 blocks/user, 768KB/block
- 推理加速：元戎（OneDNN）
- 支持 TTL 过期策略

### 2.7 Redis 服务
**端口**: 6379  
**职责**:
- 存储用户特征
- 提供高速缓存
- 支持持久化

## 3. 数据流

### 3.1 完整请求流程
```
用户
 ↓
Gateway (生成 trace_id)
 ↓
Feature (获取用户特征)
 ↓ ─────────────┐
 │              │
 ↓              ↓
Recall      Precompute
(KVWorker1)  (KVWorker2)
 │              │
 └──────┬───────┘
        ↓
Feature (获取商品信息)
 ↓
Ranking (排序)
 ↓
Gateway (返回结果)
```

### 3.2 trace_id 传递
```cpp
// Gateway 生成 trace_id
std::string trace_id = generate_uuid();

// 在每个请求中传递
request.set_trace_id(trace_id);

// 每个服务记录日志时包含 trace_id
LOG(INFO) << "Processing request, trace_id=" << trace_id;
```

## 4. 通信协议

### 4.1 BRPC 服务定义
每个服务通过 protobuf 定义接口：
- Gateway: `gateway.proto`
- Feature: `feature.proto`
- Recall: `recall.proto`
- Ranking: `ranking.proto`
- Precompute: `precompute.proto`
- KVWorker: `kvworker.proto`

### 4.2 容器间通信
```yaml
# Docker Compose 网络配置
networks:
  lingquickrec-network:
    driver: bridge

# 服务通过容器名访问
environment:
  - RECALL_HOST=recall
  - RECALL_PORT=8001
```

## 5. 监控与可观测性

### 5.1 监控指标
- **端到端时延**: Gateway 统计（平均、P99）
- **服务间通信时延**: 每个 BRPC 客户端统计
- **计算时延**: 每个服务端统计
- **KVCache 命中率**: KVWorker 统计
- **Redis 命中率**: Feature 服务统计

### 5.2 数据采集
- 轻量级探针植入每个服务的 BRPC 客户端/服务端
- 异步上报，避免影响主流程时延
- 支持 trace_id 全链路追踪

### 5.3 日志格式
```
[timestamp] [service_name] [trace_id] [level] message
示例:
2024-01-01 12:00:00.123 [gateway] [abc-123] [INFO] Request received, user_id=user_001
```

## 6. 部署架构

### 6.1 容器编排
```yaml
services:
  gateway:      # 网关服务
  feature:      # 特征服务
  recall:       # 召回服务
  ranking:      # 排序服务
  precompute:   # 前置计算服务
  kvworker:     # KVWorker（元戎）
  redis:        # Redis 缓存
```

### 6.2 资源分配
- **KVWorker**: 需要 GPU 支持（NVIDIA Docker）
- **Recall/Ranking**: CPU 密集型，可分配更多 CPU
- **Feature/Redis**: 内存密集型，可分配更多内存
- **Gateway**: I/O 密集型，平衡配置

### 6.3 网络拓扑
```
┌─────────────────────────────────────┐
│     lingquickrec-network (bridge)   │
│                                     │
│  ┌─────────┐  ┌─────────┐          │
│  │ gateway │  │ feature │          │
│  └────┬────┘  └────┬────┘          │
│       │            │                │
│  ┌────┴────────────┴────┐          │
│  │       redis          │          │
│  └──────────────────────┘          │
│                                     │
│  ┌─────────┐  ┌─────────┐          │
│  │ recall  │  │ranking  │          │
│  └────┬────┘  └────┬────┘          │
│       │            │                │
│  ┌────┴────────────┴────┐          │
│  │      kvworker        │◄── GPU   │
│  └──────────────────────┘          │
└─────────────────────────────────────┘
```

## 7. 扩展性考虑

### 7.1 水平扩展
- Gateway: 多实例 + 负载均衡
- Feature: 多实例（Redis 共享）
- Recall/Ranking: 多实例（KVWorker 共享）

### 7.2 数据分片
- KVWorker: 按 user_id 分片
- Redis: 按 key 分片

### 7.3 容错机制
- 服务超时重试
- 熔断降级
- 健康检查

## 8. 性能优化方向

### 8.1 通信优化
- BRPC 连接池复用
- 批量请求合并
- 压缩传输

### 8.2 计算优化
- 元戎算子优化
- KVCache 预取
- 特征计算缓存

### 8.3 内存优化
- KVCache 内存池
- Redis 内存淘汰策略
- Zero-copy 数据传输
