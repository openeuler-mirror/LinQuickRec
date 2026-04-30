FROM brpc_base:latest

WORKDIR /app

COPY /home/w00921547/share/vllm-0.11-0rc6+cu129-cp311-cp311-linux_aarch64.whl /tmp/vllm-0.11-0rc6+cu129-cp311-cp311-linux_aarch64.whl
RUN pip3 install /tmp/vllm-0.11-0rc6+cu129-cp311-cp311-linux_aarch64.whl && rm /tmp/vllm-0.11-0rc6+cu129-cp311-cp311-linux_aarch64.whl

COPY /home/y00909761/vllm/start_vllm_back.sh /app/start_vllm_back.sh
RUN chmod +x /app/start_vllm_back.sh

COPY /home/y00909761/vllm/start_vllm.sh /app/start_vllm.sh
RUN chmod +x /app/start_vllm.sh

COPY /home/w00921547/share/Qwen3-0.6B/ /app/models/Qwen3-0.6B/
COPY /home/w00921547/share/Qwen3-8B/ /app/models/Qwen3-8B/

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
