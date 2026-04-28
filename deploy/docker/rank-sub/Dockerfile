FROM brpc_base:v1.2

WORKDIR /app

COPY proto/ /app/proto/
COPY common/ /app/common/
COPY services/RankServiceSub/ /app/services/RankServiceSub/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/RankServiceSub -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY deploy/docker/rank-sub/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8006

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
