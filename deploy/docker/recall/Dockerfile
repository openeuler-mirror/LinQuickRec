FROM linquickrec/base:latest

ENV YR_SDK_PATH="/usr/local/lib64/python3.11/site-packages/yr/datasystem"
ENV PROXY_IP="141.1.220.189"
ENV PROXY_PORT="3128"

ENV http_proxy=http://${PROXY_IP}:${PROXY_PORT}
ENV https_proxy="${http_proxy}"
ENV HTTP_PROXY="${http_proxy}"
ENV HTTPS_PROXY="${https_proxy}"
ENV no_proxy="127.0.0.1,.huawei.com,localhost,local,.local"

# === 可配置的路径参数 ===
# vLLM 相关（仅 ENABLE_VLLM=true 时使用）
ARG VLLM_WHL=share/vllm-0.11.0rc6+cu129-cp311-cp311-linux_aarch64.whl
ARG START_VLLM_BACK=deploy/docker/recall/start_vllm_back.sh
ARG START_VLLM=deploy/docker/recall/start_vllm.sh
ARG TORCH_WHL=share/torch-2.8.0a0+gitba56102-cp311-cp311-linux_aarch64.whl
# 服务代码
ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/recall/
ARG ENTRYPOINT_FILE=deploy/docker/recall/entrypoint.sh
ARG DISCOVERY_DIR=services/discovery/

# 是否启用 vLLM（false 时跳过所有 vLLM/模型安装和拷贝，镜像大幅缩小）
ARG ENABLE_VLLM=true

WORKDIR /app

# 条件安装 vLLM、torch、triton、transformers，以及拷贝模型和启动脚本。
# 使用 --mount=type=bind 访问构建上下文，ENABLE_VLLM=false 时此步骤为空操作，
# 不产生任何镜像层内容，避免将大文件写入镜像。
RUN --mount=type=bind,source=.,target=/ctx \
    if [ "$ENABLE_VLLM" = "true" ]; then \
        pip3 install /ctx/${TORCH_WHL} && \
        pip3 install /ctx/${VLLM_WHL} && \
        pip3 install --force-reinstall \
            "fastapi==0.115.14" \
            "starlette==0.46.2" \
            "prometheus-fastapi-instrumentator==7.1.0" && \
        pip3 install /ctx/${TORCH_WHL} && \
        pip3 install pytorch-triton==3.3.0+gitab727c40 \
            --index-url https://download.pytorch.org/whl/nightly/cu129 \
            --trusted-host download.pytorch.org \
            --trusted-host download-r2.pytorch.org && \
        pip uninstall -y transformers && pip install "transformers>=4.51.1,<5" && \
        mkdir -p /app/models && \
        cp /ctx/${START_VLLM_BACK} /app/start_vllm_back.sh && \
        chmod +x /app/start_vllm_back.sh && \
        cp /ctx/${START_VLLM} /app/start_vllm.sh && \
        chmod +x /app/start_vllm.sh; \
    fi

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${SERVICE_DIR} /app/services/recall/
COPY ${DISCOVERY_DIR} /app/services/discovery/

RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/recall -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)
RUN mkdir -p /tmp/build_discovery && cd /tmp/build_discovery && \
    cmake /app/services/discovery -DCMAKE_BUILD_TYPE=Release && \
    make discovery_client -j$(nproc) && \
    cp bin/discovery_client /app/discovery_client && \
    rm -rf /tmp/build_discovery
RUN chmod +x /app/discovery_client

COPY ${ENTRYPOINT_FILE} /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8002 8000

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
