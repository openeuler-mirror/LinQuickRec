---
name: service-readme-convention
description: >
  Standard template for service-level README.md in the LinQuickRec project.
  All services SHOULD follow the 6 mandatory sections defined below.
  The proxy service README is the reference implementation.
---

## Mandatory Sections (in order)

Every service README MUST contain these 6 sections as top-level `## ` headings:

### 1. 模块简介

One-paragraph service description followed by key architectural notes (e.g.
discovery integration, error handling, observability). Subsections under this:

- **可观测性** — `### 可观测性`
  - Timing breakdown log format (if applicable)
  - Logging: `common::logger` usage

### 2. 目录结构

ASCII directory tree describing all source files and their purpose:

```
services/<name>/
├── CMakeLists.txt
├── Dockerfile
├── README.md
├── DESIGN.md
├── server/
│   ├── include/
│   │   └── <name>_server.h
│   └── src/
│       ├── main.cpp
│       └── <name>_server.cpp
├── client/
│   └── <name>_test_client.cpp
└── tests/
    └── integration_test.cpp
```

### 3. 业务流程

ASCII flowchart showing request/response pipeline, followed by:

- **阶段时序** — table describing each stage (call mode, dependency, note)
- **HTTP API 接口** — method, path, Content-Type, request/response JSON
- **error_code 编码** — format `0xMMTTCCCC` with decoding table
- **错误处理** — scenario-to-error_code mapping table
- **trace_id** — generation format and propagation mechanism

Flowchart rules:
- Use ASCII box-drawing characters (`┌ ┐ └ ┘ │ ─`)
- Box width 71 characters (from left `┌` to right `┐`)
- All text in English (no Chinese, CJK breaks monospace alignment)
- Align leading pipes (`│`) vertically

API request/response:
```
POST /<Service>/<Method>
Content-Type: application/json
```

### 4. 编译命令

Dependency table:

| 依赖 | 版本要求 | 安装参考 |
|------|----------|----------|
| CMake | >= 3.14 | `apt install cmake` |

Build command:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make <target1> <target2> -j$(nproc)
```

Output table:

| 二进制 | 说明 |
|--------|------|
| `foo_server` | 服务端 |

### 5. 启动方式

Configuration parameter table grouped by category (服务发现 / 下游服务名 / 超时 / 其他):

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--flag_name` | "default" | Description |

Direct startup command:

```bash
./build/bin/foo_server --flag=value
```

### 6. 容器搭建

Docker build:

```bash
docker build -t lingquickrec/<name>:latest -f services/<name>/Dockerfile .
```

Docker run:

```bash
docker run -p <port>:<port> lingquickrec/<name>:latest
```

### 7. 测试方法 (optional, proxy convention)

- **集成测试** — single-process integration test, no external dependencies
- **手动测试** — test client binary with example output

## Formatting Rules

### Tables

- Header separator: `|---|---|---|` (one `|---|` per column)
- Content: no trailing `|` with spaces misalignment allowed, but visual alignment preferred
- Code within tables: use backticks `` ` ``

### Code Blocks

- Shell commands: ` ```bash `
- JSON: ` ```json `
- Generic output: ` ```text ` or no language tag
- ASCII art: no language tag

### Cross-references

- Internal files: `[display](../path/to/README.md)`
- Discovery service: `[discovery 服务](../discovery/README.md)`

## Section Order

The canonical order MUST be:

1. 模块简介
2. 目录结构
3. 业务流程
4. 编译命令
5. 启动方式
6. 容器搭建
7. 测试方法 (optional)

## Quick Checklist

- [ ] All 6 mandatory sections exist in correct order
- [ ] ASCII flowchart has no Chinese characters
- [ ] Each gflag has a corresponding row in the config table
- [ ] Build section lists all binary targets
- [ ] Docker section includes both build and run commands
- [ ] Cross-references use relative paths to README.md files
- [ ] `error_code` table documents every error code the service produces
