#!/bin/bash

# 脚本功能：启动 vLLM 服务，运行 Qwen3-8B 模型
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:/usr/local/cuda/extras/CUPTI/lib64/:$LD_LIBRARY_PATH

# ===================== 配置项（可根据需要修改） =====================
MODEL_PATH="/app/models/Qwen3-0.6B/"  # 模型路径
TENSOR_PARALLEL_SIZE=1                   # 张量并行数
GPU_MEM_UTIL=0.9                         # GPU 内存利用率
MAX_MODEL_LEN=20480                      # 最大模型长度
PORT=8000                                # 服务端口
LOG_FILE="vllm_06b.log"                   # 日志文件路径
# ====================================================================

# 检查脚本是否以 bash 运行
if [ -z "$BASH_VERSION" ]; then
    echo "错误：请使用 bash 运行此脚本（bash $0），而非 sh 或其他 shell"
    exit 1
fi

# 检查模型路径是否存在
if [ ! -d "$MODEL_PATH" ]; then
    echo "错误：模型路径不存在 -> $MODEL_PATH"
    exit 1
fi

# 检查端口是否被占用
if lsof -i:$PORT > /dev/null 2>&1; then
    echo "警告：端口 $PORT 已被占用！"
    read -p "是否继续启动（y/n）？" -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "用户取消启动，脚本退出"
        exit 0
    fi
fi

# 停止已有同名进程（可选，根据需要开启）
# echo "停止已有 vllm serve 进程..."
# pkill -f "vllm serve $MODEL_PATH" > /dev/null 2>&1

# 启动 vLLM 服务
echo "========================================"
echo "开始启动 vLLM 服务..."
echo "模型路径: $MODEL_PATH"
echo "端口: $PORT"
echo "日志文件: $LOG_FILE"
echo "========================================"

nohup vllm serve "$MODEL_PATH" \
    --dtype auto \
    --tensor-parallel-size "$TENSOR_PARALLEL_SIZE" \
    --enable-prefix-caching \
    --gpu-memory-utilization "$GPU_MEM_UTIL" \
    --max-model-len "$MAX_MODEL_LEN" \
    --port "$PORT" \
    # --data-parallel-size 1 \
    # --data-parallel-rpc-port 8101
    2>&1 > "$LOG_FILE" &

# 记录进程 ID
VLLM_PID=$!
echo "vLLM 服务已启动，进程 ID: $VLLM_PID"
echo "日志实时查看命令：tail -f $LOG_FILE"
echo "停止服务命令：kill $VLLM_PID （或 pkill -f 'vllm serve $MODEL_PATH'）"

# 验证启动（可选）
sleep 5
if ps -p $VLLM_PID > /dev/null 2>&1; then
    echo "vLLM 服务启动成功！"
else
    echo "vLLM 服务启动失败，请查看日志：cat $LOG_FILE"
    exit 1
fi