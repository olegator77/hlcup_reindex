FROM ubuntu:latest

RUN apt-get update && \
    apt-get install -y build-essential g++ libsnappy-dev make curl git screen psmisc

RUN curl -L https://github.com/google/leveldb/archive/v1.20.tar.gz | tar xzv && \
    cd leveldb-1.20 && make -j8 && mv out-static/libleveldb.* /usr/local/lib && \
    cd include && cp -R leveldb /usr/local/include && \
    ldconfig && \
    cd .. && rm -rf leveldb-1.20

RUN git clone https://github.com/wg/wrk.git && cd wrk && \
    make -j8 && cp wrk /usr/bin && \
    cd .. && rm -rf wrk

RUN curl -L http://dist.schmorp.de/libev/libev-4.24.tar.gz | tar xzv && \
    cd libev-4.24 && \
    ./configure --disable-shared && \
    make -j4 && \
    make install && \
    cd .. && rm -rf libev-4.24

ADD . /src

RUN apt-get -y install unzip

WORKDIR src
RUN make -B -j8 && make install && cd .. && rm -rf src

EXPOSE 80

# Create entry point script
RUN echo "#!/bin/bash\n" \
    "unzip -o /tmp/data/data.zip -d /go/data\n" \
    "cp /tmp/data/options.txt /go/data/options.txt\n" \
    "cat /proc/cpuinfo\n" \
    "exec \"\$@\"\n" >/ep.sh && chmod +x /ep.sh

RUN mkdir -p /go/data

ENTRYPOINT ["/ep.sh"]

CMD ["/usr/bin/hlcup_reindex"]

