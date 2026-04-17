# 服务端口配置

本文档记录了搜推广系统中所有服务的端口配置。

## 服务端口列表

| 服务名称 | 端口号 | 说明 | 配置文件 |
|---------|--------|------|---------|
| vLLM | 8000 | vLLM 模型推理服务 | `vllm/Dockerfile` |
| Proxy | - | 网关服务，负责接收客户端请求并转发到各子服务 | `services/Proxy/server/proxy_server.cpp` |
| RecallService | 8001 | 召回服务，负责从大模型获取 SKU ID 列表 | `services/recall/server/recall_server.cpp` |
| RecallKVWorker | 8002 | 元戎数据系统 Worker（Recall 专用），负责 KV 缓存读写 | 元戎默认配置 |
| FeatureService | 8003 | 特征服务，负责处理用户特征数据 | `services/FeatureService/server/feature_server.cpp` |
| PrecalcService | 8004 | 前置计算服务，负责生成前置计算结果并写入 KVWorker | `services/PrecalcService/server/precalc_server.cpp` |
| RankService | 8005 | 精排服务，负责对召回的 SKU 进行排序 | `services/RankService/server/rank_server.cpp` |
| RankKVWorker | 8006 | 元戎数据系统 Worker（Rank 专用），负责 KV 缓存读写 | 元戎默认配置 |
| Redis | 6379 | Redis 缓存服务，用于特征存储 | `redis/redis.conf` |

## 端口分配原则

1. **8000-8009**: 业务服务端口
   - 8000: vLLM 模型服务
   - 8001: RecallService
   - 8002: RecallKVWorker
   - 8003: FeatureService
   - 8004: PrecalcService
   - 8005: RankService
   - 8006: RankKVWorker

2. **6379**: 基础设施服务端口
   - 6379: Redis

## 配置方式

### 服务端配置

每个服务通过命令行参数 `--server_port` 配置监听端口：

```bash
# Recall 服务
./recall_server --server_port=8001

# Precalc 服务
./precalc_server --server_port=8004 \
    --kvworker_host=recall_kvworker \
    --kvworker_port=8002

# Rank 服务
./rank_server --server_port=8005
```

### 客户端配置

客户端通过 `--server` 参数指定要连接的服务地址：

```bash
# 连接 Recall 服务
./recall_test_client --server=127.0.0.1:8001

# 连接 Precalc 服务
./precalc_test_client --server=127.0.0.1:8004
```

## 服务调用关系

```
Client (任意端口)
    ↓
Proxy (网关)
    ↓
├─→ RecallService (8001) ──→ RecallKVWorker (8002)
├─→ FeatureService (8003) ──→ Redis (6379)
├─→ PrecalcService (8004) ──→ RecallKVWorker (8002)
└─→ RankService (8005) ──→ RankKVWorker (8006)
```

## Docker 容器端口映射

在 Docker 部署中，需要将容器端口映射到宿主机：

```yaml
# docker-compose.yml 示例
services:
  vllm:
    ports:
      - "8000:8000"
  
  recall:
    ports:
      - "8001:8001"
  
  recall_kvworker:
    ports:
      - "8002:8002"
  
  feature:
    ports:
      - "8003:8003"
  
  precalc:
    ports:
      - "8004:8004"
  
  rank:
    ports:
      - "8005:8005"
  
  rank_kvworker:
    ports:
      - "8006:8006"
  
  redis:
    ports:
      - "6379:6379"
```

## 端口冲突处理

如果遇到端口冲突，可以通过以下方式解决：

1. **修改服务端口**: 使用 `--server_port` 参数指定其他端口
2. **检查端口占用**: 使用 `netstat -tulpn | grep <端口号>` 查看端口占用情况
3. **停止冲突服务**: 停止占用端口的其他服务

## 端口修改指南

如果需要修改某个服务的端口：

1. 修改服务的默认端口配置（`DEFINE_int32(server_port, XXXX, "...")`）
2. 更新相关客户端的连接配置
3. 更新 Docker 配置文件中的端口映射
4. 更新本文档

## 相关文档

- [API 文档](API.md) - 各服务的接口定义
- [架构文档](../README.md) - 系统整体架构说明
