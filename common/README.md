# common — 公共基础库

## 目录结构

```
common/
├── include/common/          # 对外公开头文件
│   ├── error.h              # 错误码体系（Status / ErrorCode / ErrorBuilder）
│   ├── logger.h             # 日志系统（LoggerConfig / 流式宏 / 条件日志 / TraceId）
│   ├── thread_pool.h        # 线程池（ThreadPool）
│   ├── global_thread_pool.h # 全局单例线程池
│   └── internal/            # 内部头文件
│       ├── error/
│       └── logger/
├── src/                     # 源文件
│   ├── error/
│   ├── logger/
│   └── thread_pool/
├── tests/                   # 单元测试（使用 assert）
│   ├── test_error.cpp
│   └── test_thread_pool.cpp
├── examples/                # 使用示例
│   ├── error_example.cpp
│   ├── logger_example.cpp
│   └── thread_pool_example.cpp
└── CMakeLists.txt
```

## 编译

```bash
# 从 common 目录运行
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

单独编译某个目标：

```bash
make test_error -j$(nproc)
make test_thread_pool -j$(nproc)
make error_example -j$(nproc)
make logger_example -j$(nproc)
make thread_pool_example -j$(nproc)
```

## 运行测试

```bash
./tests/test_error
./tests/test_thread_pool
```

## 运行示例

```bash
./examples/error_example
./examples/logger_example

# 线程池示例（可通过 gflags 配置线程数和任务数）
./examples/thread_pool_example --global_thread_pool_size=4 --tasks=100
```

## 日志系统占位符说明

通过 `LoggerConfig::pattern` 自定义日志输出格式，支持的占位符：

| 占位符 | 含义 | 输出示例 |
|--------|------|---------|
| `%Y` | 年（4位） | `2026` |
| `%m` | 月（2位） | `04` |
| `%d` | 日（2位） | `29` |
| `%H` | 时（24小时制，2位） | `11` |
| `%M` | 分（2位） | `14` |
| `%S` | 秒（2位） | `17` |
| `%e` | 毫秒（3位） | `410` |
| `%l` | 日志级别 | `INFO` |
| `%t` | 线程 ID | `1234567890` |
| `%f` | 文件名（不含路径） | `logger_example.cpp` |
| `%F` | 文件完整路径 | `/workspace/.../logger_example.cpp` |
| `%L` | 行号 | `37` |
| `%c` | 函数名 | `main` |
| `%T` | Trace ID（需 `enable_trace_id=true`） | `trace-abc-123` |
| `%v` | 日志消息内容 | `Service started` |

### 常用组合示例

**短格式（仅时间 + 级别）：**
```cpp
cfg.pattern = "[%H:%M:%S] [%l] %v";
// [11:14:17] [INFO ] Service started
```

**默认格式（时间 + 级别 + 线程）：**
```cpp
cfg.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v";
// [2026-04-29 11:14:17.410] [INFO ] [1234567890] Service started
```

**长格式（完整上下文）：**
```cpp
cfg.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%f:%L] [%c] [%t] [%T] %v";
// [2026-04-29 11:14:17.410] [INFO ] [main.cpp:37] [main] [1234567890] [trace-abc] Service started
```

完整示例见 `examples/logger_example.cpp`。
