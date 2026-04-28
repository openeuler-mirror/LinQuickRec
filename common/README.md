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
