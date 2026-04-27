FROM lingquickrec/base AS builder

WORKDIR /project
COPY . .
RUN mkdir -p build && cd build && cmake /project && make discovery_client -j$(nproc)

WORKDIR /project/services/discovery/examples
RUN mkdir -p build && cd build && cmake /project/services/discovery/examples && make -j$(nproc) && \
    g++ -std=c++17 -O2 -o /usr/bin/pseudo_service pseudo_service/main.cpp

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libgcc-s1 && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /project/build/discovery_client /usr/bin/
COPY --from=builder /usr/bin/pseudo_service /usr/bin/
COPY --from=builder /project/services/discovery/examples/build/test_discover /usr/bin/
COPY services/discovery/examples/entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/bin/sh", "/entrypoint.sh"]
