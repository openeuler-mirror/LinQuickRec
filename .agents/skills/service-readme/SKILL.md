---
name: service-readme
description: >
  Standard template for service-level README.md in the LinQuickRec project.
  All services SHOULD follow the 6 mandatory sections defined below.
  The proxy service README is the reference implementation.
---

## Mandatory Sections (in order)

Every service README MUST contain these 6 sections as top-level `## ` headings.
Services may add supplementary `## ` sections after all mandatory sections
(e.g. `## 端口对照表`).

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

ASCII flowchart showing request/response pipeline.

Flowchart rules:
- Use ASCII box-drawing characters (`┌ ┐ └ ┘ │ ─`)
- Box width 71 characters (from left `┌` to right `┐`)
- All text in English (no Chinese, CJK breaks monospace alignment)
- Align leading pipes (`│`) vertically

And other description.

### 4. 编译命令

Dependency table:

| 依赖 | 版本要求 | 备注 |
|------|----------|------|
| CMake | - | 编译工具链 |
| brpc | - | `linquickrec/base:latest` 基础镜像已内置 |
| protobuf | - | `linquickrec/base:latest` 基础镜像已内置 |
| abseil-cpp | - | `linquickrec/base:latest` 基础镜像已内置 |

#### 脚本构建

```bash
bash build.sh       # Release 构建
bash build.sh debug # Debug 构建
```

#### 手动构建

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make <target1> <target2> -j$(nproc)
```

#### 编译产物

| 二进制 | 说明 |
|--------|------|
| `<name>_server` | 服务端 |
| `<name>_test_client` | 手动测试客户端（如有） |
| `<name>_integration_test` | 单进程集成测试（如有） |

### 5. 启动方式

#### 启动命令

```bash
./build/bin/<name>_server \
    --server_port=<port> \
    --discovery_addr="discovery-server:8100"
```

#### 配置参数

Table columns: `| 参数 | 类型 | 默认值 | 说明 |`.
Rows grouped by category, each category introduced by a bold separator row
(e.g. `| **服务发现** | | | |`).

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **服务发现** | | | |
| `--discovery_addr` | string | "127.0.0.1:8100" | Discovery server 地址 |
| ... | | | |

Each gflag MUST have a row. Per-downstream channel parameters (timeout,
backup_request, max_retry, connect_timeout, connection_type) may use a
separate sub-table if numerous.

### 6. 容器搭建

#### 构建镜像

```bash
# 在项目根目录下执行
docker build -t linquickrec/<name>:latest \
  -f deploy/docker/<name>/Dockerfile .
```

#### 运行容器

```bash
docker run -d --name <name>-service \
  -p <port>:<port> \
  -e REGISTRY_BACKEND=etcd \
  -e ETCD_ENDPOINTS=<etcd-ip>:2379 \
  linquickrec/<name>:latest
```

If the service supports both `etcd` and `discovery_server` backends, include
both variants.

### 7. 测试方法 (optional, proxy convention)

- **集成测试** — single-process integration test, no external dependencies.
  - Docker: `docker run --rm linquickrec/<name>:latest test`
  - Local: `./build/bin/<name>_integration_test`
- **手动测试** — test client binary with example output.
  - Via `docker exec` or local: `./build/bin/<name>_test_client`

## Formatting Rules

### Tables

- Header separator: `|---|---|---|` (one `|---|` per column)
- Content: no trailing `|` with spaces misalignment allowed, but visual alignment preferred
- Code within tables: use backticks `` ` ``
- Section separator rows in config tables: bold text spanning all columns (e.g. `| **服务发现** | | | |`)

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
    - `### 可观测性` (optional)
2. 目录结构
3. 业务流程
4. 编译命令
    - `### 脚本构建`
    - `### 手动构建`
    - `### 编译产物`
5. 启动方式
    - `### 启动命令`
    - `### 配置参数`
6. 容器搭建
    - `### 构建镜像`
    - `### 运行容器`
7. 测试方法 (optional)
    - `### 集成测试`
    - `### 手动测试`
8+ 补充章节 (optional, any)

## Quick Checklist

- [ ] All 6 mandatory sections exist in correct order
- [ ] `编译命令` has `### 脚本构建`, `### 手动构建`, `### 编译产物` subsections
- [ ] `启动方式` has `### 启动命令`, `### 配置参数` subsections
- [ ] `容器搭建` has `### 构建镜像`, `### 运行容器` subsections
- [ ] ASCII flowchart has no Chinese characters
- [ ] Each gflag has a corresponding row in the config table
- [ ] Config tables include column "类型" alongside "参数", "默认值", "说明"
- [ ] `编译产物` table lists all binary targets
- [ ] Docker section includes both build and run commands
- [ ] Cross-references use relative paths to README.md files
- [ ] `error_code` table documents every error code the service produces
- [ ] Supplementary sections (if any) appear after all mandatory sections
