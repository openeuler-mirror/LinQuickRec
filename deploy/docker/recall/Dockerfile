FROM brpc_base:latest

ENV PROXY_IP="141.1.95.55"
ENV PROXY_PORT="3128"

ENV http_proxy=http://${PROXY_IP}:${PROXY_PORT}
ENV https_proxy="${http_proxy}"
ENV HTTP_PROXY="${http_proxy}"
ENV HTTPS_PROXY="${https_proxy}"
ENV no_proxy="127.0.0.1,.huawei.com,localhost,local,.local"

# === 可配置的路径参数 ===
# vLLM 相关
ARG VLLM_WHL=share/vllm-0.11.0rc6+cu129-cp311-cp311-linux_aarch64.whl
ARG START_VLLM_BACK=share/vllm/start_vllm_back.sh
ARG START_VLLM=share/vllm/start_vllm.sh
ARG MODEL_QWEN_06B=share/Qwen3-0.6B/
ARG MODEL_QWEN_8B=share/Qwen3-8B/
ARG TORCH_WHL=share/torch-2.8.0a0+gitba56102-cp311-cp311-linux_aarch64.whl
# 服务代码
ARG PROTO_DIR=proto/
ARG COMMON_DIR=common/
ARG SERVICE_DIR=services/recall/
ARG ENTRYPOINT_FILE=deploy/docker/recall/entrypoint.sh
ARG DISCOVERY_CLIENT_BIN=build/bin/discovery_client

WORKDIR /app

COPY ${TORCH_WHL} /tmp/torch-2.8.0a0+gitba56102-cp311-cp311-linux_aarch64.whl
RUN pip3 install /tmp/torch-2.8.0a0+gitba56102-cp311-cp311-linux_aarch64.whl

COPY ${VLLM_WHL} /tmp/vllm-0.11.0rc6+cu129-cp311-cp311-linux_aarch64.whl
RUN pip3 install /tmp/vllm-0.11.0rc6+cu129-cp311-cp311-linux_aarch64.whl && rm /tmp/vllm-0.11.0rc6+cu129-cp311-cp311-linux_aarch64.whl

RUN pip3 install /tmp/torch-2.8.0a0+gitba56102-cp311-cp311-linux_aarch64.whl && rm /tmp/torch-2.8.0a0+gitba56102-cp311-cp311-linux_aarch64.whl

RUN pip3 install pytorch-triton==3.3.0+gitab727c40 \
--index-url https://download.pytorch.org/whl/nightly/cu129 \
--trusted-host download.pytorch.org \
--trusted-host download-r2.pytorch.org

RUN pip uninstall -y transformers && pip install "transformers>=4.51.1,<5"

COPY ${START_VLLM_BACK} /app/start_vllm_back.sh
RUN chmod +x /app/start_vllm_back.sh

COPY ${START_VLLM} /app/start_vllm.sh
RUN chmod +x /app/start_vllm.sh

COPY ${MODEL_QWEN_06B} /app/models/Qwen3-0.6B/
COPY ${MODEL_QWEN_8B} /app/models/Qwen3-8B/

COPY ${PROTO_DIR} /app/proto/
COPY ${COMMON_DIR} /app/common/
COPY ${SERVICE_DIR} /app/services/recall/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/recall -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY ${DISCOVERY_CLIENT_BIN} /app/discovery_client
RUN chmod +x /app/discovery_client

COPY ${ENTRYPOINT_FILE} /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8001 8000

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
