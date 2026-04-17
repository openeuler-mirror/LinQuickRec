# PrecalcService 实现任务列表

## 任务概览
实现 PrecalcService 服务端和客户端，支持向精排 KVWorker 写入前置计算结果。

## 任务列表

- [x] **Task 1**: 修改 recall_server.cpp，支持自定义 SKU 数量
  - [ ] SubTask 1.1: 添加 `--sku_count` 配置参数（默认 1000）
  - [ ] SubTask 1.2: 修改 `parse_vllm_response` 函数，根据 `sku_count` 截取结果
  - [ ] SubTask 1.3: 更新客户端测试代码，支持设置 `sku_count`

- [x] **Task 2**: 创建 API 接口文档
  - [ ] SubTask 2.1: 创建 `docs/API.md` 文件
  - [ ] SubTask 2.2: 详细说明 Proxy 服务接口
  - [ ] SubTask 2.3: 详细说明 FeatureService 接口
  - [ ] SubTask 2.4: 详细说明 RecallService 接口
  - [ ] SubTask 2.5: 详细说明 PrecalcService 接口
  - [ ] SubTask 2.6: 详细说明 RankService 接口

- [x] **Task 3**: 更新 README.md
  - [ ] SubTask 3.1: 更新系统架构图
  - [ ] SubTask 3.2: 更新容器列表
  - [ ] SubTask 3.3: 更新目录结构
  - [ ] SubTask 3.4: 更新服务说明

- [x] **Task 4**: 实现 PrecalcService 服务端
  - [ ] SubTask 4.1: 阅读并确认 `precalc.proto` 消息定义
  - [ ] SubTask 4.2: 设计前置计算模拟逻辑（需用户确认）
  - [ ] SubTask 4.3: 实现 `precalc_server.cpp`：
    - BRPC 服务框架
    - 前置计算模拟逻辑
    - 通过元戎写入 KVWorker
    - Key 生成（user_id + 时间戳）
    - TTL 设置（5 秒）
  - [ ] SubTask 4.4: 添加配置参数（结果大小、TTL 等）

- [x] **Task 5**: 实现 PrecalcService 客户端
  - [ ] SubTask 5.1: 创建 `precalc_client.cpp`
  - [ ] SubTask 5.2: 构造测试请求
  - [ ] SubTask 5.3: 调用 PrecalcService
  - [ ] SubTask 5.4: 验证响应

- [x] **Task 6**: 创建 PrecalcService 构建配置
  - [ ] SubTask 6.1: 创建 `CMakeLists.txt`（参考 RecallService）
  - [ ] SubTask 6.2: 配置 Proto 代码生成
  - [ ] SubTask 6.3: 添加元戎依赖

## 任务依赖关系

- Task 2 和 Task 3 可以并行执行
- Task 4 依赖于 Task 1 完成
- Task 5 依赖于 Task 4 完成
- Task 6 可以与 Task 4、Task 5 并行执行

## 验收标准

1. ✅ RecallService 支持自定义 SKU 数量
2. ✅ API 文档完整详细
3. ✅ README 反映最新架构
4. ✅ PrecalcService 服务端正常运行
5. ✅ PrecalcService 客户端测试通过
6. ✅ 构建配置正确，编译通过
