# LingQuickRec 配置参考

## 关于配置级别

| 配置级别 | 含义 |
|---------|------|
| **必须指定** | 无默认值或环境相关，部署前必须配置 |
| **允许调整** | 有默认值，可根据场景修改 |
| **不建议修改** | 有合理默认值，极少需要变更 |

## 全局配置

| 参数 | 默认值 | 配置级别 | 说明 |
|------|--------|---------|------|
| `DISCOVERY_ADDR` | discovery-server:8100 | 允许调整 | Discovery 服务地址（容器网络下使用容器名） |
| `DISCOVERY_PORT` | 8100 | 允许调整 | 服务发现中心端口 |
| `PROXY_PORT` | 8080 | 允许调整 | 网关对外 HTTP 端口 |

## Discovery

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8100 | 允许调整 | 监听端口 |
| `--heartbeat_check_interval_ms` | int32 | 1000 | 不建议修改 | 健康检查扫描间隔 (ms) |
| `--heartbeat_grace_factor` | double | 2.0 | 不建议修改 | 心跳超时倍数 |
| `--cleanup_factor` | double | 5.0 | 不建议修改 | 清理倍数 |

## Proxy

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--server_port` | int32 | 8080 | 允许调整 | HTTP 监听端口 |
| `--discovery_addr` | string | — | 必须指定 | Discovery 地址（取自全局 `DISCOVERY_ADDR`） |
| `--host` | string | "auto" | 不建议修改 | 本容器 IP |
| `--heartbeat_interval` | int32 | 5 | 不建议修改 | 心跳间隔 (秒) |
| `--health_check_timeout` | int32 | 2 | 不建议修改 | TCP 探测超时 (秒) |
| `--fail_threshold` | int32 | 3 | 不建议修改 | 连续失败次数阈值 |
| `--startup_timeout` | int32 | 30 | 不建议修改 | 等待主服务就绪超时 (秒) |

## Discovery Client（sidecar）

所有服务容器通过 discovery_client 向 discovery server 注册。以下参数由 entrypoint.sh 注入。`--discovery_addr` 取自全局配置 `DISCOVERY_ADDR`，其余参数均有合理默认值，通常无需修改。

| 参数 | 类型 | 默认值 | 配置级别 | 说明 |
|------|------|--------|---------|------|
| `--service_type` | string | — | 必须指定 | 服务类型名 |
| `--service_port` | int32 | — | 必须指定 | 本容器主服务端口 |
| `--discovery_addr` | string | (全局 `DISCOVERY_ADDR`) | 必须指定 | Discovery 地址 |
| `--host` | string | "auto" | 不建议修改 | 本容器 IP |
| `--heartbeat_interval` | int32 | 5 | 不建议修改 | 心跳间隔 (秒) |
| `--health_check_timeout` | int32 | 2 | 不建议修改 | TCP 探测超时 (秒) |
| `--fail_threshold` | int32 | 3 | 不建议修改 | 连续失败次数阈值 |
| `--startup_timeout` | int32 | 30 | 不建议修改 | 等待主服务就绪超时 (秒) |
