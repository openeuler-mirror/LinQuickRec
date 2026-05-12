# 特征服务 — FeatureService

## 模块简介

FeatureService 是推荐系统的特征层，负责根据用户 ID 从 Redis 等数据源获取用户特征数据（行为日志、画像标签等），为下游服务（RecallService、PrecalcService）提供特征输入。

当前处于开发阶段，实现尚未合入 main 分支。

## 目录结构

```
services/feature/
├── build.sh
├── CMakeLists.txt
├── DESIGN.md
├── README.md
├── server/
│   ├── include/
│   │   └── feature_server.h
│   └── src/
│       ├── feature_server.cpp
│       └── main.cpp
```

## 编译命令

| 依赖 | 版本要求 | 安装参考 |
|------|----------|----------|
| CMake | >= 3.14 | `apt install cmake` |
| brpc | >= 1.4 | `linquickrec/base:latest` 基础镜像已内置 |
| protobuf | >= 3.0 | `linquickrec/base:latest` 基础镜像已内置 |
| abseil-cpp | latest | `linquickrec/base:latest` 基础镜像已内置 |

### 脚本构建

```bash
cd services/feature
./build.sh              # 默认为 Release 构建
./build.sh release      # Release 构建
./build.sh debug        # Debug 构建
./build.sh clean        # 清理后构建
```

### 手动构建

```bash
cd services/feature
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make feature_server -j$(nproc)
```

### 编译产物

| 二进制 | 说明 |
|--------|------|
| `feature_server` | 特征服务端 |

## 启动方式

待实现后补充。

## 容器搭建

待实现后补充。
