# Recall 服务部署和使用指南

## 📋 目录

1. [代码提交到代码仓](#1-代码提交到代码仓)
2. [服务器下载和编译](#2-服务器下载和编译)
3. [运行和测试](#3-运行和测试)
4. [常见问题](#4-常见问题)

---

## 1. 代码提交到代码仓

### 1.1 初始化 Git 仓库（如果是第一次）

```bash
cd D:\LingQuickRec\Vllm-brpc-Gateway

# 初始化 git 仓库
git init

# 添加所有文件
git add .

# 创建第一次提交
git commit -m "Initial commit: Recall service with RapidJSON and ThreadPool"
```

### 1.2 添加远程仓库（如果已有代码仓）

```bash
# 添加远程仓库（替换为您的仓库地址）
git remote add origin <your-repo-url>

# 例如：
# git remote add origin https://github.com/yourname/Vllm-brpc-Gateway.git
# git remote add origin git@github.com:yourname/Vllm-brpc-Gateway.git
```

### 1.3 提交代码

```bash
# 查看状态
git status

# 添加修改的文件
git add .

# 提交
git commit -m "Update recall service with RpcController support and new proto format"

# 推送到远程仓库
git push origin main
```

### 1.4 使用 .gitignore（可选但推荐）

创建 `.gitignore` 文件，排除不需要提交的文件：

```bash
# Build directory
build/
*/build/
*/*/build/

# Generated files
*.pb.cc
*.pb.h

# IDE files
.vscode/
.idea/
*.swp
*.swo

# Backup files
*.backup
```

---

## 2. 服务器下载和编译

### 2.1 登录服务器

```bash
ssh user@your-server-ip
```

### 2.2 下载代码

```bash
# 进入工作目录
cd /path/to/your/workspace

# 克隆仓库
git clone <your-repo-url>

# 进入项目目录
cd Vllm-brpc-Gateway
```

### 2.3 检查依赖

确保服务器已安装以下依赖：

```bash
# 检查 BRPC
which brpc_config || echo "BRPC not found"

# 检查 Protobuf
which protoc || echo "Protobuf not found"

# 检查 RapidJSON
ls /usr/include/rapidjson || echo "RapidJSON not found"

# 检查 gflags
which pkg-config && pkg-config --exists gflags || echo "gflags not found"
```

### 2.4 安装缺失的依赖（如果需要）

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libbrpc-dev \
    libprotobuf-dev \
    protobuf-compiler \
    rapidjson-dev \
    libgflags-dev \
    libssl-dev \
    libcurl4-openssl-dev

# CentOS/RHEL
sudo yum install -y \
    gcc gcc-c++ \
    cmake \
    protobuf-devel \
    rapidjson-devel \
    gflags-devel \
    openssl-devel \
    libcurl-devel
```

### 2.5 编译项目

```bash
# 进入 recall 服务目录
cd services/recall

# 创建 build 目录
mkdir build && cd build

# 配置 CMake
cmake ..

# 编译
make -j$(nproc)

# 或者使用特定数量的核心
# make -j4
```

### 2.6 编译输出

编译成功后会生成两个可执行文件：

```bash
ls -lh vllm_server vllm_client

# 输出示例：
# -rwxrwxr-x 1 user user 5.2M Jan 15 10:30 vllm_server
# -rwxrwxr-x 1 user user 2.1M Jan 15 10:30 vllm_client
```

---

## 3. 运行和测试

### 3.1 启动 vLLM 服务（如果未启动）

确保 vLLM 和 Qwen3-0.6B 已启动：

```bash
# 示例：启动 vLLM（根据您的实际配置调整）
python -m vllm.entrypoints.api_server \
    --model /workspace/share/Qwen3-0.6B \
    --host 127.0.0.1 \
    --port 8000
```

### 3.2 启动 Recall 服务

```bash
# 在后台启动服务
./vllm_server \
    --server_port=8001 \
    --vllm_base_url="http://127.0.0.1:8000" \
    --vllm_endpoint="/v1/chat/completions" \
    --model_name="/workspace/share/Qwen3-0.6B" \
    --thread_pool_size=128 \
    --vllm_timeout_ms=50000 \
    --logtostderr &

# 查看日志
tail -f /tmp/vllm_server.INFO
```

### 3.3 测试服务

```bash
# 运行客户端测试
./vllm_client \
    --server="127.0.0.1:8001" \
    --user_id=12345 \
    --log_count=3
```

### 3.4 预期输出

**客户端输出示例**：
```
========================================
Recall Service Client Test
========================================
Request:
  user_id: 12345
  user_logs count: 3
  other: test_request

Sending request to 127.0.0.1:8001

========================================
Response:
========================================
SKU IDs count: 5
SKU IDs: 1001, 1002, 1003, 1004, 1005
========================================
Test completed successfully!
========================================
```

**服务端日志示例**：
```
I0115 10:30:00.123456 12345 brpc_server.cpp:391] Recall request received, user_id: 12345
I0115 10:30:00.234567 12345 brpc_server.cpp:439] Converted request to JSON: {"user_id":12345,...}
I0115 10:30:00.345678 12345 brpc_server.cpp:443] Built vLLM request: {"model":"/workspace/share/Qwen3-0.6B",...}
I0115 10:30:01.456789 12345 brpc_server.cpp:479] vLLM response: {"choices":[{"message":{"content":"1001,1002,1003,1004,1005"}}],...}
I0115 10:30:01.567890 12345 brpc_server.cpp:354] Successfully parsed 5 SKU IDs from response
I0115 10:30:01.678901 12345 brpc_server.cpp:405] Recall request processed successfully, user_id: 12345, sku_count: 5
```

### 3.5 停止服务

```bash
# 找到进程 ID
ps aux | grep vllm_server

# 停止服务
kill <pid>

# 或者强制停止
kill -9 <pid>
```

---

## 4. 常见问题

### 4.1 编译错误

**错误 1：找不到 rapidjson**
```
fatal error: rapidjson/document.h: No such file or directory
```
**解决**：
```bash
sudo apt-get install rapidjson-dev
# 或
sudo yum install rapidjson-devel
```

**错误 2：找不到 protobuf**
```
fatal error: google/protobuf/message.h: No such file or directory
```
**解决**：
```bash
sudo apt-get install libprotobuf-dev protobuf-compiler
```

**错误 3：找不到 brpc**
```
fatal error: brpc/server.h: No such file or directory
```
**解决**：
- 检查 BRPC 安装路径
- 更新 CMakeLists.txt 中的 `BRPC_INCLUDE_DIR`

### 4.2 运行时错误

**错误 1：连接 vLLM 失败**
```
vLLM service error: Connection refused
```
**解决**：
1. 检查 vLLM 是否启动：`ps aux | grep vllm`
2. 检查端口：`netstat -tlnp | grep 8000`
3. 检查 URL 配置：`--vllm_base_url`

**错误 2：端口已被占用**
```
Failed to start server on port 8001
```
**解决**：
```bash
# 查找占用端口的进程
netstat -tlnp | grep 8001

# 停止进程或更改端口
kill <pid>
# 或
./vllm_server --server_port=8002
```

**错误 3：解析 vLLM 响应失败**
```
No SKU IDs parsed from response content
```
**解决**：
1. 检查 vLLM 返回的格式
2. 查看服务端日志中的原始响应
3. 调整 `parse_vllm_response()` 函数的解析逻辑

### 4.3 性能问题

**问题：响应慢**
```bash
# 检查线程池大小
./vllm_server --thread_pool_size=256

# 检查 vLLM 响应时间
tail -f /tmp/vllm_server.INFO | grep "vLLM response"

# 检查并发请求数
# 查看日志中的请求处理时间
```

**问题：内存占用高**
```bash
# 查看内存使用
ps aux | grep vllm_server

# 调整线程池大小
./vllm_server --thread_pool_size=64
```

---

## 5. 快速参考

### 5.1 常用命令

```bash
# 编译
cd services/recall/build && make -j4

# 启动服务
./vllm_server --server_port=8001 --logtostderr &

# 测试
./vllm_client --server="127.0.0.1:8001"

# 查看日志
tail -f /tmp/vllm_server.INFO

# 停止服务
pkill vllm_server
```

### 5.2 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8001 | 服务端口 |
| `--vllm_base_url` | http://127.0.0.1:8000 | vLLM 地址 |
| `--vllm_endpoint` | /v1/chat/completions | vLLM 端点 |
| `--model_name` | /workspace/share/Qwen3-0.6B | 模型名称 |
| `--thread_pool_size` | 128 | 线程池大小 |
| `--vllm_timeout_ms` | 5000 | vLLM 超时（毫秒） |

### 5.3 文件位置

```
services/recall/
├── server/brpc_server.cpp        # 服务端代码
├── client/brpc_client.cpp        # 客户端代码
├── CMakeLists.txt                # 构建配置
└── build/
    ├── vllm_server               # 服务端可执行文件
    └── vllm_client               # 客户端可执行文件
```

---

## 6. 更新代码

### 6.1 本地更新后推送

```bash
# 本地提交
git add .
git commit -m "Your changes"
git push origin main
```

### 6.2 服务器拉取更新

```bash
# 进入项目目录
cd Vllm-brpc-Gateway

# 拉取最新代码
git pull origin main

# 重新编译
cd services/recall/build
make -j4

# 重启服务
pkill vllm_server
./vllm_server --server_port=8001 --logtostderr &
```

---

## 7. 联系和支持

如有问题，请查看：
- 服务端日志：`/tmp/vllm_server.INFO`
- 客户端输出
- 本部署文档
