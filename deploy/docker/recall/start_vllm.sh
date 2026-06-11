#!/bin/bash

# 脚本功能：启动 vLLM 服务，运行 Qwen3-8B 模型
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:/usr/local/cuda/extras/CUPTI/lib64/:$LD_LIBRARY_PATH

# ===================== 配置项（可根据需要修改） =====================
MODEL_PATH=${VLLM_MODEL_PATH}            # 模型路径，由环境变量传入
TENSOR_PARALLEL_SIZE=1                   # 张量并行数
GPU_MEM_UTIL=0.8                         # GPU 内存利用率
MAX_MODEL_LEN=20480                      # 最大模型长度
PORT=8000                                # 服务端口
LOG_FILE="vllm_0.6b.log"                 # 日志文件路径
# ====================================================================

# 检查模型路径是否存在
if [ ! -d "$MODEL_PATH" ]; then
    echo "错误：模型路径不存在 -> $MODEL_PATH"
    exit 1
fi

# 检查端口是否被占用
if lsof -i:$PORT > /dev/null 2>&1; then
    echo "警告：端口 $PORT 已被占用！"
    exit 0
fi

# 启动 vLLM 服务
echo "========================================"
echo "开始启动 vLLM 服务..."
echo "模型路径: $MODEL_PATH"
echo "端口: $PORT"
echo "日志文件: $LOG_FILE"
echo "========================================"

vllm serve "$MODEL_PATH" \
    --dtype auto \
    --tensor-parallel-size "$TENSOR_PARALLEL_SIZE" \
    --enable-prefix-caching \
    --gpu-memory-utilization "$GPU_MEM_UTIL" \
    --max-model-len "$MAX_MODEL_LEN" \
    --port "$PORT" \
    2>&1 > "$LOG_FILE" 
