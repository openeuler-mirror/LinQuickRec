# PrecalcService 服务器部署指南

## 部署环境要求

### 1. 服务器配置
- **操作系统**: Linux (Ubuntu 20.04/22.04 推荐)
- **编译器**: GCC 9+ 或 Clang 10+
- **CMake**: 3.16+
- **依赖库**: BRPC, Protocol Buffers, gflags, glog, 元戎 SDK

### 2. 元戎环境
- 需要安装 openYuanrong datasystem
- KVWorker 需要 GPU 支持（如果需要测试完整的 KV 缓存功能）

## 部署步骤

### 步骤 1: 准备服务器环境

```bash
# SSH 登录服务器
ssh username@server_ip

# 创建项目目录
mkdir -p ~/projects/LingQuickRec
cd ~/projects/LingQuickRec

# 克隆项目（如果项目在 git 仓库）
git clone <repository_url> .

# 或者使用 scp 上传本地代码
# 在本地执行：
# scp -r d:/LingQuickRec/Vllm-brpc-Gateway/* username@server_ip:~/projects/LingQuickRec/
```

### 步骤 2: 安装依赖

```bash
# 安装系统依赖（Ubuntu/Debian）
sudo apt-get update
sudo apt-get install -y build-essential cmake git libssl-dev protobuf-compiler \
    libprotobuf-dev libgflags-dev libgoogle-glog-dev libgtest-dev

# 安装 BRPC（如果未安装）
# 参考：https://github.com/apache/incubator-brpc/blob/master/docs/cn/build_brpc.md

# 安装元戎 SDK
# 参考元戎官方文档进行安装
pip install https://openyuanrong.obs.cn-southwest-2.myhuaweicloud.com/release/0.7.0/linux/x86_64/openyuanrong-0.7.0-cp311-cp311-manylinux_2_34_x86_64.whl
```

### 步骤 3: 编译项目

```bash
cd ~/projects/LingQuickRec

# 创建构建目录
mkdir -p build
cd build

# 配置 CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译 PrecalcService
make precalc_server -j$(nproc)
make precalc_test_client -j$(nproc)

# 编译完成后，可执行文件在 build/services/PrecalcService/ 目录
```

### 步骤 4: 启动 KVWorker（如果需要）

```bash
# 如果 KVWorker 是独立服务，先启动它
# 假设 KVWorker 在 8002 端口
./kvworker_server --port=8002 &

# 或者使用 systemd 管理
sudo systemctl start kvworker
```

### 步骤 5: 启动 PrecalcServer

```bash
cd ~/projects/LingQuickRec/build/services/PrecalcService

# 启动服务端
./precalc_server \
    --server_port=8004 \
    --kvworker_host=127.0.0.1 \
    --kvworker_port=8002 \
    --precalc_result_size_mb=8.5 \
    --ttl_seconds=5 \
    --response_total_size_kb=100 \
    --enable_timing_stats=true \
    --logtostderr

# 后台运行（使用 nohup）
nohup ./precalc_server \
    --server_port=8004 \
    --kvworker_host=127.0.0.1 \
    --kvworker_port=8002 \
    --enable_timing_stats=true \
    --logtostderr > precalc_server.log 2>&1 &

# 查看进程
ps aux | grep precalc_server

# 查看日志
tail -f precalc_server.log
```

### 步骤 6: 运行客户端测试

```bash
cd ~/projects/LingQuickRec/build/services/PrecalcService

# 运行客户端
./precalc_test_client \
    --server=127.0.0.1:8004 \
    --user_feat_size_kb=100 \
    --precalc_result_size_mb=8.5 \
    --response_total_size_kb=100 \
    --logtostderr

# 多次测试（例如 100 次）
for i in {1..100}; do
    ./precalc_test_client --server=127.0.0.1:8004 --logtostderr 2>&1 | grep "Client timing"
done
```

### 步骤 7: 查看时延统计

```bash
# 查看服务端日志
tail -f precalc_server.log | grep "timing breakdown"

# 查看客户端日志
./precalc_test_client --server=127.0.0.1:8004 2>&1 | grep "Client timing"

# 收集时延数据
grep "Server timing breakdown" precalc_server.log | awk -F'=' '{print $2, $3}' > server_timing.txt
grep "Client timing breakdown" precalc_client.log | awk -F'=' '{print $2, $3}' > client_timing.txt
```

## 配置说明

### 服务端参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server_port` | 8004 | 服务端监听端口 |
| `--kvworker_host` | 127.0.0.1 | KVWorker 主机地址 |
| `--kvworker_port` | 31501 | KVWorker 端口 |
| `--precalc_result_size_mb` | 8.5 | 前置计算结果大小（MB） |
| `--ttl_seconds` | 5 | TTL 时间（秒） |
| `--response_total_size_kb` | 100 | 响应总大小（KB） |
| `--enable_timing_stats` | true | 是否启用时延统计 |

### 客户端参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server` | 127.0.0.1:8004 | 服务器地址 |
| `--user_feat_size_kb` | 100 | 用户特征数据大小（KB） |
| `--precalc_result_size_mb` | 8.5 | 期望的前置计算结果大小（MB） |
| `--response_total_size_kb` | 100 | 期望的响应总大小（KB） |

## 测试脚本示例

### 批量测试脚本

创建 `test_timing.sh`：

```bash
#!/bin/bash

SERVER="127.0.0.1:8004"
NUM_REQUESTS=100
CLIENT="./precalc_test_client"

echo "Starting timing test with $NUM_REQUESTS requests..."

for i in $(seq 1 $NUM_REQUESTS); do
    $CLIENT --server=$SERVER --logtostderr 2>&1 | \
        grep "Client timing breakdown" | \
        awk -v req=$i '{print "Request " req ": " $0}'
    
    sleep 0.1  # 避免请求过快
done

echo "Test completed!"
```

### 性能分析脚本

创建 `analyze_timing.sh`：

```bash
#!/bin/bash

LOG_FILE="precalc_server.log"

echo "=== Server Timing Analysis ==="
echo "KVWrite Cost Statistics:"
grep "kvwrite_cost=" $LOG_FILE | \
    awk -F'kvwrite_cost=' '{print $2}' | \
    awk -F' ms' '{print $1}' | \
    sort -n | \
    awk '{sum+=$1; count++; if(min=="")min=$1; max=$1} END {print "Min: "min" ms, Max: "max" ms, Avg: "sum/count" ms, Count: "count}'

echo ""
echo "Server Process Total Statistics:"
grep "server_process_total=" $LOG_FILE | \
    awk -F'server_process_total=' '{print $2}' | \
    awk -F' ms' '{print $1}' | \
    sort -n | \
    awk '{sum+=$1; count++; if(min=="")min=$1; max=$1} END {print "Min: "min" ms, Max: "max" ms, Avg: "sum/count" ms, Count: "count}'
```

## 常见问题

### 1. 端口被占用

```bash
# 查看端口占用
netstat -tulpn | grep 8004

# 杀死占用端口的进程
kill -9 <PID>

# 或者修改服务端端口
./precalc_server --server_port=8005
```

### 2. KVWorker 连接失败

```bash
# 检查 KVWorker 是否运行
ps aux | grep kvworker

# 测试 KVWorker 连通性
telnet 127.0.0.1 8002

# 检查防火墙
sudo ufw status
sudo ufw allow 8002/tcp
```

### 3. 编译错误

```bash
# 清理构建缓存
cd build
make clean
rm -rf *

# 重新配置和编译
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 4. 元戎 SDK 未找到

```bash
# 检查 SDK 安装
python -c "import yr; print(yr.__file__)"

# 设置环境变量
export YR_SDK_PATH=/path/to/yuanrong/sdk
export LD_LIBRARY_PATH=$YR_SDK_PATH/lib:$LD_LIBRARY_PATH
```

## 监控和调试

### 使用 strace 调试

```bash
# 跟踪系统调用
strace -f -o precalc.strace ./precalc_server --server_port=8004

# 查看网络相关系统调用
grep "socket\|connect\|sendto\|recvfrom" precalc.strace
```

### 使用 perf 性能分析

```bash
# 安装 perf
sudo apt-get install linux-tools-common linux-tools-generic

# 运行性能分析
perf record -g ./precalc_server --server_port=8004

# 查看报告
perf report
```

### 实时监控

```bash
# 监控 CPU 和内存
top -p $(pgrep precalc_server)

# 监控网络
iftop -P -p precalc_server

# 监控磁盘 I/O
iotop -o -p $(pgrep precalc_server)
```

## 部署检查清单

- [ ] 服务器环境已准备（编译器、CMake、依赖库）
- [ ] 元戎 SDK 已安装
- [ ] 项目代码已上传到服务器
- [ ] 编译成功，生成可执行文件
- [ ] KVWorker 已启动并运行
- [ ] PrecalcServer 已启动，监听正确端口
- [ ] 客户端可以连接到服务端
- [ ] 时延统计日志正常输出
- [ ] 测试脚本运行正常
- [ ] 性能数据已收集

## 下一步

1. **压力测试**: 使用多个客户端并发测试
2. **长时间运行**: 观察内存泄漏和性能衰减
3. **调优**: 根据时延数据优化配置参数
4. **监控**: 集成监控系统（如 Prometheus + Grafana）
