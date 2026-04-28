FROM brpc_base:latest

COPY build/bin/discovery_client /usr/bin/
COPY services/discovery/examples/build/bin/pseudo_service /usr/bin/
COPY services/discovery/examples/build/bin/test_discover /usr/bin/
COPY services/discovery/examples/build/bin/test_register /usr/bin/
COPY services/discovery/examples/build/bin/test_heartbeat_cycle /usr/bin/
COPY services/discovery/examples/entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/bin/sh", "/entrypoint.sh"]
