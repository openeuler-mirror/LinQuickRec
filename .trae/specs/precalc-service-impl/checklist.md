# PrecalcService 实现检查清单

## 代码实现检查

- [x] Task 1: RecallService SKU 数量可配置
  - [ ] `recall_server.cpp` 中添加 `--sku_count` 参数
  - [ ] 响应中 SKU 数量正确截取
  - [ ] 默认值为 1000

- [x] Task 2: API 接口文档
  - [ ] `docs/API.md` 文件创建
  - [ ] 所有 5 个服务接口都有详细说明
  - [ ] 包含请求/响应消息定义
  - [ ] 包含使用示例

- [x] Task 3: README.md 更新
  - [ ] 系统架构图更新
  - [ ] 容器列表完整
  - [ ] 目录结构正确
  - [ ] 服务说明准确

- [x] Task 4: PrecalcService 服务端
  - [ ] `precalc_server.cpp` 实现
  - [ ] BRPC 服务框架正确
  - [ ] 前置计算模拟逻辑实现
  - [ ] 元戎通信集成
  - [ ] Key 生成逻辑正确（user_id + 时间戳）
  - [ ] TTL 设置为 5 秒
  - [ ] 配置参数完整

- [x] Task 5: PrecalcService 客户端
  - [ ] `precalc_client.cpp` 实现
  - [ ] 测试请求构造正确
  - [ ] 响应验证通过

- [x] Task 6: 构建配置
  - [ ] `CMakeLists.txt` 创建
  - [ ] Proto 代码生成配置正确
  - [ ] 元戎依赖添加

## 功能验证检查

- [x] RecallService SKU 数量测试
  - [ ] 默认 1000 个 SKU
  - [ ] 自定义数量工作正常

- [x] PrecalcService 功能测试
  - [ ] 服务端启动成功
  - [ ] 客户端调用成功
  - [ ] Key 生成格式正确
  - [ ] TTL 自动删除生效
  - [ ] 前置计算结果大小正确（8.5MB 默认）

- [x] 编译测试
  - [ ] PrecalcService 编译通过
  - [ ] 无编译错误和警告

## 文档完整性检查

- [x] API 文档完整
- [x] README 更新
- [x] 代码注释完整
- [x] 配置参数说明清晰
