FROM linquickrec/base:latest

WORKDIR /app

COPY proto/ /app/proto/
COPY common/ /app/common/
COPY services/discovery/ /app/services/discovery/

# Build discovery_client from main discovery project
RUN mkdir -p /app/build/discovery && cd /app/build/discovery && \
    cmake ../../services/discovery -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) discovery_client && \
    cp /app/build/discovery/bin/discovery_client /usr/bin/

# Build examples (pseudo_service + test tools)
RUN mkdir -p /app/build/examples && cd /app/build/examples && \
    cmake ../../services/discovery/examples -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && \
    cp /app/build/examples/bin/* /usr/bin/

COPY services/discovery/examples/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

ENTRYPOINT ["/bin/sh", "/entrypoint.sh"]
