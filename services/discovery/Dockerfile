FROM brpc_base:latest

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/usr-local.conf && ldconfig

ARG DISCOVERY_SERVER_BIN=build/bin/discovery_server
COPY $DISCOVERY_SERVER_BIN /usr/bin/discovery_server

EXPOSE 8100

ENTRYPOINT ["discovery_server"]
CMD ["--server_port=8100"]
