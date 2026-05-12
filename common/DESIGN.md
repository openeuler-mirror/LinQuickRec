# common — 公共基础库设计文档

## 1. 概述

common 库为 LingQuickRec 系统的所有服务提供三个基础公共设施：错误码体系、日志系统、线程池。设计原则如下：

- **零外部依赖** — 仅使用 C++17 标准库和 pthread，无需任何第三方库即可编译
- **线程安全** — 三个组件均支持多线程并发访问
- **服务可定制** — 各服务可通过配置选择日志级别、输出目标、线程池大小等
- **无侵入** — 通过头文件 + 宏接口对业务代码影响最小化，可按需选择使用的组件

[image: common 库在全项目中的位置关系图]

---

## 2. 错误码体系

### 2.1 设计目标

错误码体系旨在解决微服务架构下跨服务错误传递的一致性问题。每个错误码独立编码，接收方无需查表即可解码出错误来源模块、错误类型和具体原因。

### 2.2 错误码编码格式

```
 0x MM TT CCCC
 │   │  │   └── 具体错误码 (16bit, 0-65535)
 │   │  └────── 错误类型 (8bit)
 │   └────────── 模块代码 (8bit)
 └────────────── 固定前缀 0x
```

**模块代码表：**

| 值 | 模块 | 说明 |
|---|---|---|
| 0x00 | COMMON | 公共库内部错误 |
| 0x01 | GATEWAY | 网关服务（Proxy） |
| 0x02 | FEATURE | 特征服务 |
| 0x03 | RECALL | 召回服务 |
| 0x04 | PRECALC | 前置计算服务 |
| 0x05 | RANK_MASTER | 精排主图服务 |
| 0x06 | KVWORKER | 元戎分布式缓存 |
| 0x07 | REDIS | Redis 缓存 |
| 0x08 | VLLM | 大模型推理 |
| 0x09 | DISCOVERY | 服务发现中心 |
| 0x0A | RANK_SUB | 精排子图服务 |

**错误类型表：**

| 值 | 类型 | 说明 |
|---|---|---|
| 0x00 | SUCCESS | 无错误 |
| 0x01 | INVALID_INPUT | 参数校验失败 |
| 0x02 | RESOURCE_ERROR | 系统资源不足 |
| 0x03 | SERVICE_ERROR | 下游服务调用失败 |
| 0x04 | TIMEOUT | 操作超时 |
| 0x05 | NOT_FOUND | 数据不存在 |
| 0x06 | UNAUTHORIZED | 鉴权失败 |
| 0x07 | CONFIG_ERROR | 配置错误 |
| 0x08 | NETWORK_ERROR | 网络通信异常 |
| 0x0F | INTERNAL | 系统内部异常 |

### 2.3 关键组件

**ErrorCode 工具函数**（`error_code.h`）：

提供 `MakeErrorCode()`、`GetModuleFromCode()`、`GetTypeFromCode()`、`GetSpecificCodeFromCode()` 等 constexpr 函数，用于编码和解码。错误码的校验可通过 `IsSuccessCode()` 完成。

**Status 类**（`status.h`）：

封装 `error_code + error_message`，提供以下接口：

- `Status::OK()` — 创建成功状态
- `Status::Error(code, message)` — 创建错误状态
- `IsOk()` / `IsError()` — 状态判断
- `Code()` / `Message()` — 访问错误信息
- `ToString()` — 格式化为可读字符串
- `operator bool()` — 支持 `if (status)` 语法

**各服务专用错误码**：

在每个服务的命名空间中预定义常用错误码常量，例如 `recall_errors::VLLM_REQUEST_FAILED`、`precalc_errors::KVCLIENT_SET_FAILED` 等，避免魔法数字。

### 2.4 使用方式

函数返回 `common::error::Status`，调用方通过 `if (!status)` 或 `status.IsOk()` 判断：

```
Status ProcessRequest(...) {
    auto status = ValidateInput(...);
    if (!status) {
        return Status::Error(common_errors::INVALID_ARGUMENT, "input is empty");
    }
    // ... 业务逻辑
    return Status::OK();
}
```

---

## 3. 日志系统

### 3.1 设计目标

统一全项目的日志输出格式和方式，同时兼容 brpc 的流式日志语法（`LOG(INFO) << "msg"`），避免同一项目中出现多种日志风格。支持运行时切换输出目标（控制台 / 文件），以及通过 pattern 字符串自定义日志格式。

[image: 日志系统的整体架构图，展示 Logger 单例 -> Sink 列表的层次关系]

### 3.2 架构

日志系统采用 **Singleton + Sink 模式**：

```
Logger (单例)
├── LoggerConfig          ── 日志级别、pattern、async 等配置
├── LogSink[]             ── 输出目标列表（可同时输出到多个目标）
│   ├── ConsoleSink       ── 输出到 std::clog
│   └── FileSink          ── 输出到文件，支持大小轮转
├── LogStream             ── 临时对象，在析构时将流式内容提交给 Logger
└── TraceIdGetter         ── 回调函数，获取当前线程的 trace_id
```

**Logger 单例**：

通过 `Logger::Instance()` 获取，全局唯一。初始化时需要传入 `LoggerConfig` 结构，配置日志级别、输出格式模板（pattern）、是否启用文件输出、是否启用异步日志等。

**LogSink 抽象**：

定义 `Write(message)` 和 `Flush()` 接口，派生类决定输出的具体目的地：

- **ConsoleSink**：输出到 `std::clog`，使用 `mutex_` 保证多线程下的输出不交错
- **FileSink**：输出到指定文件路径，支持按大小轮转。当当前文件达到 `max_file_size` 时，自动重命名文件并创建新文件，保留最近 `max_files` 个历史文件

**LogStream**：

临时对象，通过 `operator<<` 收集日志内容，在析构时调用 `Logger::Log()` 将完整消息提交给 Logger 处理。宏 `LOG_STREAM(level)` 创建一个 LogStream 对象，自动携带 `__FILE__`、`__LINE__`、`__FUNCTION__` 等信息。

**异步日志**：

当 `LoggerConfig::async = true` 时，日志消息先入队列，由后台线程批量写入 sink，减少业务线程的 I/O 阻塞。通过 `queue_size` 和 `flush_interval_ms` 控制队列大小和刷新间隔。

### 3.3 宏接口

为了最小化对业务代码的侵入，日志系统提供与 brpc 兼容的宏：

| 宏 | 用法 | 说明 |
|---|---|---|
| `LOG_TRACE` / `LOG_DEBUG` / `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` / `LOG_FATAL` | `LOG_INFO << "msg" << value;` | 流式日志 |
| `LOG_IF(level, condition)` | `LOG_IF(INFO, count > 0) << count;` | 条件日志 |
| `LOG_INFO_FMT(format, ...)` | `LOG_INFO_FMT("user_id={}", id);` | 格式化日志（需 fmtlib） |

为避免与 brpc 的 `LOG(INFO)` 宏冲突，定义了 `COMMON_LOGGER_COMPAT_MODE` 开关。开启后提供 `LOG(level)` 宏覆盖 brpc 的同名宏。

### 3.4 Pattern 格式

通过 `LoggerConfig::pattern` 字符串自定义每行日志的输出格式。支持的占位符覆盖时间戳、级别、位置信息、trace_id 等 15 个维度，详见 `README.md` 说明。

内置三个常用模板：

```
短格式：  [%H:%M:%S] [%l] %v
默认：    [%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v
长格式：  [%Y-%m-%d %H:%M:%S.%e] [%l] [%f:%L] [%c] [%t] [%T] %v
```

### 3.5 Trace ID 集成

全链路追踪需要 trace_id 在上下游服务之间传递。日志系统通过 `SetTraceIdGetter()` 注册一个回调函数，Logger 在格式化日志消息时调用该函数获取当前线程的 trace_id，并作为 `%T` 占位符填入日志。当请求从一个服务传递到下一个服务时，trace_id 通过 BRPC 的 `cntl.set_log_id()` 传递，下游服务在入口处更新本地的 trace_id 回调返回值，从而实现跨服务的日志关联。

---

## 4. 线程池

### 4.1 设计目标

提供轻量级、自包含的线程池，用于将耗时任务（如下游 RPC 调用）异步化，提高系统吞吐量。既支持独立实例，也提供全局单例。

### 4.2 核心实现

```
ThreadPool (固定 N 个线程)
├── workers_[N]           ── N 个工作线程，构造函数中创建
├── tasks_                ── std::queue<std::function<void()>>
├── queue_mutex_          ── 保护任务队列
├── condition_            ── 通知线程有新任务
└── stop_                 ── 原子标志，控制线程退出
```

**工作流程：**

1. 业务线程调用 `submit(f, args...)` 将任务打包为 `std::packaged_task` 入队
2. 通知空闲工作线程取任务执行
3. 调用方通过返回的 `std::future` 获取执行结果或异常
4. 析构时设置 `stop_ = true`，唤醒所有线程，等待所有任务完成

**线程安全设计：**

- **任务队列**：使用 `mutex` 保护 `push` 和 `pop` 操作，确保多线程安全
- **停止标志**：使用 `atomic<bool>` 避免锁竞争
- **通知机制**：入队后 `notify_one()`，停止时 `notify_all()`
- **生命周期**：已 `stop` 的线程池拒绝新任务，防止野指针访问

### 4.3 全局线程池

为了简化使用，提供全局单例 `common::get_global_thread_pool()`。线程数通过 gflag `--global_thread_pool_size` 配置：

- **默认值**：根据 CPU 核数自动计算（通常为 `std::thread::hardware_concurrency()`）
- **手动配置**：启动时传入 `--global_thread_pool_size=8` 覆盖

在 Proxy 等需要并发调用多个下游服务的场景中，使用全局线程池并行执行 Recall 和 Precalc 调用，无需为每个请求创建和销毁线程。

[image: 线程池在 Proxy 中用于并行调用下游的时序图]

### 4.4 使用场景

- Proxy 服务中并行调用 RecallService 和 PrecalcService
- 任何需要异步执行且无需自定义线程池的场景
- 替代手动 `std::thread` 管理，减少资源泄漏风险
