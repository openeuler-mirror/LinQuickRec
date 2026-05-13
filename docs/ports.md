# 服务端口配置

## 服务端口列表

| 服务名称 | 端口号 | 说明 | 实现状态 |
|---------|--------|------|---------|
| vLLM | 8000 | 大模型推理服务 | 基础设施 |
| Feature | 8001 | 特征服务 | 待合入 |
| Recall | 8002 | 召回服务 | ✅ 已完成 |
| Precalc | 8003 | 前置计算服务 | ✅ 已完成 |
| RankMaster | 8004 | 精排主图服务 | ✅ 已完成 |
| RankSub | 8005 | 精排子图服务 | ✅ 已完成 |
| Proxy | 8080 | 网关服务 | ✅ 已完成 |
| Discovery | 8100 | 服务发现中心 | ✅ 已完成 |
| KVWorker (Recall) | 31501 | 元戎数据系统 Worker（远程） | 由元戎提供服务 |
| KVWorker (Rank) | 31502 | 元戎数据系统 Worker（远程） | 由元戎提供服务 |
| Redis | 6379 | 缓存服务 | 基础设施 |

## 端口分配原则

1. **8000-8009**: 本地业务服务端口
   - 8000: vLLM 模型服务
   - 8001: Feature
   - 8002: Recall
   - 8003: Precalc
   - 8004: RankMaster
   - 8005: RankSub

2. **31500-31599**: 远程 KVWorker 端口
   - 31501: KVWorker (Recall, 141.61.84.245)
   - 31502: KVWorker (Rank, 141.61.84.245)

3. **6379**: 基础设施服务端口
   - 6379: Redis

4. **8080**: 网关服务端口
   - 8080: Proxy

5. **8100**: 服务发现端口
   - 8100: Discovery

## 服务调用关系

```
Client (任意端口)
    ↓
Proxy (8080)
    ↓
├─→ Feature (8001) ──→ Redis (6379)
├─→ Recall (8002) ──→ vLLM (8000)
├─→ Precalc (8003) ──→ KVWorker Rank (31502)
└─→ RankMaster (8004) ──┬─→ RankSub (8005) ──→ KVWorker Rank (31502)
                        ├─→ RankSub (8005) ──→ KVWorker Rank (31502)
                        └─→ RankSub (8005) ──→ KVWorker Rank (31502)
                        (N 个子图，默认 10 个)
```

## 相关文档

- [API 文档](API.md) - 各服务的接口定义
- [架构文档](../README.md) - 系统整体架构说明
