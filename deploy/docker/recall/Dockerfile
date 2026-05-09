FROM brpc_base:v1.3

ARG VLLM_WHL_PATH=/path/to/vllm.whl
ARG VLLM_SCRIPT_PATH=/path/to/run_vllm.sh
ARG QWEN_MODEL_PATH=/path/to/Qwen3-0.6B

WORKDIR /app

COPY ${VLLM_WHL_PATH} /tmp/vllm.whl
RUN pip3 install /tmp/vllm.whl && rm /tmp/vllm.whl

COPY ${VLLM_SCRIPT_PATH} /app/run_vllm.sh
RUN chmod +x /app/run_vllm.sh

COPY ${QWEN_MODEL_PATH} /app/models/Qwen3-0.6B/

COPY proto/ /app/proto/
COPY common/ /app/common/
COPY services/recall/ /app/services/recall/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/recall -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY deploy/docker/recall/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8001 8000

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
