FROM brpc_base:latest

COPY build/discovery_client /usr/bin/
COPY services/discovery/examples/build/pseudo_service /usr/bin/
COPY services/discovery/examples/build/test_discover /usr/bin/
COPY services/discovery/examples/build/test_register /usr/bin/
COPY services/discovery/examples/build/test_heartbeat_cycle /usr/bin/
COPY services/discovery/examples/entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/bin/sh", "/entrypoint.sh"]
