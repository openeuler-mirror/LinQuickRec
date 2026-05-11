FROM linquickrec/base:latest

RUN pip install openyuanrong_datasystem*.whl

COPY start_datasystem.sh /workerspace/
COPY example.py /workerspace/

WORKDIR /workerspace

CMD /workerspace/start_datasystem.sh; python /workerspace/example.py; tail -f /dev/null
