# TODO.md — Perf 可观测性系统

每个任务的标题格式：`[模块] 简述`。每个任务必须包含**可测试方式**和**成功约束**（精确的量化标准）。

---

## 阶段 0: 基础设施 (拆分 PERF 与业务日志)

### [0.1] PerfRingBuffer 头文件 + 实现

- **文件**: `common/include/common/perf_registry.h` + `common/src/perf/perf_registry.cpp`
- **内容**: lock-free ring buffer (CAS, 预分配数组, 单写者-单消费者模型)
- **接口**: `Push(Span)`, `Snapshot() → vector<Span> + dropped_count`, `Reset()`
- **大小**: 默认 50,000 条，通过 `PERF_RING_SIZE` 环境变量控制
- **可测试方式**: 编译独立的单元测试程序，spawn 10 线程并发 Push 10,000 次，验证 `Snapshot().size() == 100,000`，且无 crash、无 data race (TSan 通过)
- **成功约束**: 
  - Push 延迟 < 50ns (benchmark, warm cache)
  - 内存占用 < 1MB (sizeof(Span) × 50000 = 5.8MB)
  - dropped_count 在 overflow 时精确等于溢出次数

### [0.2] 修改 perf::Log — 双写到 RingBuffer

- **文件**: `common/include/common/perf_logger.h`
- **改动**: 在 `perf::Log()` 末尾追加 `PerfRingBuffer::Push(span)`，ring buffer 不存在时 Push 是 no-op (atomic_null ptr)
- **不改变 LOG_INFO 行为** (调试时仍可 kubectl logs 看 PERF)
- **可测试方式**: 启动 proxy 服务，curl 一条请求，停顿 2s 让 LOG_INFO 写盘，再 curl /debug/perf 确认 span 列表非空
- **成功约束**: 
  - perf::Log 调用增加 < 10ns (conditional atomic store)
  - 现有集成测试 `proxy_integration_test` 全部通过

### [0.3] 各服务注册 /debug/perf HTTP handler

- **文件**: 各服务 `main.cpp` (proxy, feature, recall, rank-sub, precalc-and-rank-master)
- **改动**: 新增 `brpc::Server::AddService` 调用注册 handler
- **JSON 格式**: `{"spans":[{...},{...}], "dropped":0}`
- **可测试方式**: 
  ```bash
  curl http://127.0.0.1:8080/debug/perf | jq '.spans | length'
  ```
  预期输出 >= 9 (proxy 单次请求至少生成 9 条 span)
- **成功约束**: 
  - /debug/perf 返回时间 < 1ms (50,000 条 span 序列化为 JSON)
  - 所有 5 个服务均可独立 curl 访问

### [0.4] 修改 FileSink — std::endl → '\n'

- **文件**: `common/src/logger/file_sink.cpp` Line 139
- **改动**: `<< std::endl` 替换为 `<< '\n'` (去掉强制 flush)
- **可测试方式**: 每秒 10,000 条 LOG_INFO 写入，检查 p99 写入延迟是否下降 > 50%
- **成功约束**: 对 stdout 输出无影响 (buffered cout 在程序退出时自动 flush)

---

## 阶段 1: perf-collector 核心服务

### [1.1] 项目骨架

- **目录**: `services/perf/collector/`
- **内容**: CMakeLists.txt, Dockerfile, entrypoint.sh, main.cpp
- **依赖**: brpc, absl, sqlite3, rapidjson
- **可测试方式**: `docker compose up perf-collector`, curl `GET /api/v1/health`
- **成功约束**: 容器启动 3s 内返回 `{"status":"ok"}`, rss < 20MB

### [1.2] Puller — 定时拉取各服务 /debug/perf

- **文件**: `services/perf/collector/src/puller.cpp`
- **行为**: 每秒遍历服务列表，BRPC call `/debug/perf`，parse JSON，累积 SpanBatch
- **自适应**: 连续 3 次 dropped > 0 → 拉取间隔降至 500ms
- **可测试方式**: 发送 100 请求到 proxy，等 3 秒让 puller 拉取，curl /api/v1/metrics 确认 `spans_pulled >= 900`
- **成功约束**: puller 延迟 < 50ms；连接超时 2s；自动跳过不可达服务

### [1.3] StatsEngine — 实时统计

- **文件**: `services/perf/collector/src/stats_engine.cpp`
- **算法**: Welford 增量统计 (avg, stddev) + Reservoir Sampling (1%) 用于分位数
- **粒度**: 全局 (all-time) + 滑窗 (1m, 5m, 15m) + 按 stage 分组
- **可测试方式**: 序列化注入已知分布 [1..100]，验证 avg=50.5(±0.01), p50=50(±1), p99=99(±1)
- **成功约束**: 单次 Push 延迟 < 1µs; p99 与 numpy.percentile 误差 < 1%; 1 万条 Push 后内存 < 1MB

### [1.4] SQLite Store — 持久化

- **文件**: `services/perf/collector/src/sqlite_store.cpp`
- **行为**: 每 1 秒 batch Flush (1000 rows/transaction, WAL mode, 64MB cache)
- **索引**: ts_us, (stage,ts_us), trace_id, series_id
- **留存**: 每天清理 > 30 天数据
- **可测试方式**: 写入 10,000 条 span，重启 collector，验证 count 不变
- **成功约束**: 重启后数据不丢失；读延迟 < 10ms

---

## 阶段 2: 系列管理 + 离群值检测

### [2.1] Series API

- **文件**: `services/perf/collector/src/series_manager.cpp`
- **API**: `POST /series/start?name=baseline`, `POST /series/stop`, `GET /series`, `GET /series/{id}/stats`
- **可测试方式**: 创建系列，发 50 请求，stop，确认 count >= 50
- **成功约束**: 两个并发系列互不干扰；重启后系列状态恢复

### [2.2] Outlier Detection

- **文件**: `services/perf/collector/src/outlier_detector.cpp`
- **判定**: `duration_ms > avg + k × stddev` (k 默认 3.0)
- **可测试方式**: 注入 [10×99, 1000×1]，验证离群值 count == 1
- **成功约束**: 离群值列表含 {trace_id, duration_ms, deviation_ratio}；阈值实时可调

---

## 阶段 3: WebUI

### [3.1] Dashboard — 实时概览

- **文件**: `services/perf/webui/`
- **功能**: QPS 折线图, p99/p50/avg 仪表盘, 延迟分布直方图
- **可测试方式**: 打开页面 → 确认自动刷新 → 确认数据与 /api/v1/stats/recent 一致
- **成功约束**: 加载 < 1s; 10,000 数据点渲染不卡顿

### [3.2] Trace Waterfall — 请求链路

- **功能**: 输入 trace_id → 展示完整链路瀑布图 (proxy_e2e 到 rank_sub_total)
- **可测试方式**: 复制 trace_id → 输入 → 确认所有 stage 按时间排列
- **成功约束**: 横条宽度等比缩放; 不存在时显示 "not found"

### [3.3] Compare — A/B 系列对比

- **功能**: 选择 2 个系列 → 并列 {avg, p99, stddev, count} 表 + 差值百分比
- **可测试方式**: 系列 A(100条) → 改 flag → 系列 B(100条) → 对比页确认差异
- **成功约束**: 差值列绿/红标注

---

## 阶段 4: 生产加固

### [4.1] 高可用

- **collector 重启**: 从 SQLite 恢复 running_stats (每 10s checkpoint)
- **collector 宕机**: 业务服务不受影响 (ring buffer 自旋)
- **可测试方式**: 发 100 请求 → kill collector → 重启 → 验证 stats 恢复
- **成功约束**: 崩溃恢复后丢失 < 10s 数据

### [4.2] 降级策略

- **collector 不可达**: puller 重试 3 次 × 2s → 跳过 → WARNING log → 下轮继续
- **ring buffer overflow**: dropped 计数 → collector 发现 → HTTP 503 响应
- **SQLite 满盘**: 停止 Flush，内存暂存 → ERROR log → HTTP 503
- **可测试方式**: 手动填满 SQLite → 确认不 crash → 确认 /health 返回 "degraded"
- **成功约束**: 降级状态下原数据仍可查询，新数据不丢

---

## 附录 A: 改动清单

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| `common/include/common/perf_registry.h` | **新建** | Span/PerfRingBuffer/PerfRingRegistry 声明 |
| `common/src/perf/perf_registry.cpp` | **新建** | Snapshot, Init 实现 |
| `common/include/common/perf_logger.h` | 修改 | 追加 Registry::Push() |
| `common/CMakeLists.txt` | 修改 | 新增 perf_registry 源文件 |
| `common/src/logger/file_sink.cpp` | 修改 | endl → '\n' |
| 各服务 `main.cpp` (5 个) | 修改 | 新增 /debug/perf BRPC service handler |
| `services/perf/collector/` | **新建** | perf-collector 服务 |
| `services/perf/webui/` | **新建** | WebUI |
| `deploy/docker/docker-compose.yml` | 修改 | 新增 perf-collector service |
| `deploy/k8s/` | 修改 | 新增 perf-collector YAML |
