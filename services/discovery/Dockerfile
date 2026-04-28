FROM brpc_base:latest

COPY build/bin/discovery_server /usr/bin/

EXPOSE 8100

ENTRYPOINT ["discovery_server"]
CMD ["--server_port=8100"]
