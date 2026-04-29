FROM brpc_base:latest

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/usr-local.conf && ldconfig

ARG DISCOVERY_CLIENT_BIN=build/bin/discovery_client
ARG PSEUDO_SERVICE_BIN=services/discovery/examples/build/bin/pseudo_service
ARG TEST_DISCOVER_BIN=services/discovery/examples/build/bin/test_discover
ARG TEST_REGISTER_BIN=services/discovery/examples/build/bin/test_register
ARG TEST_HEARTBEAT_CYCLE_BIN=services/discovery/examples/build/bin/test_heartbeat_cycle
COPY $DISCOVERY_CLIENT_BIN /usr/bin/discovery_client
COPY $PSEUDO_SERVICE_BIN /usr/bin/pseudo_service
COPY $TEST_DISCOVER_BIN /usr/bin/test_discover
COPY $TEST_REGISTER_BIN /usr/bin/test_register
COPY $TEST_HEARTBEAT_CYCLE_BIN /usr/bin/test_heartbeat_cycle
COPY services/discovery/examples/entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/bin/sh", "/entrypoint.sh"]
