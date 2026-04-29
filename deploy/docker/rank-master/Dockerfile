FROM brpc_base:v1.3

WORKDIR /app

COPY proto/ /app/proto/
COPY common/ /app/common/
COPY services/RankServiceMaster/ /app/services/RankServiceMaster/
RUN mkdir -p /app/build && cd /app/build && \
    cmake ../services/RankServiceMaster -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

COPY deploy/docker/rank-master/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

EXPOSE 8005

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/app/entrypoint.sh"]
