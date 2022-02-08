# Dockerfile for libcsp and friends
FROM ubuntu:20.04 AS builder-image

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y  --no-install-recommends build-essential git python3 wget ca-certificates apt-transport-https pkg-config libczmq-dev python3-dev libsocketcan-dev

# get certs so https to github will work
RUN mkdir -p /usr/local/share/ca-certificates/cacert.org
RUN wget -P /usr/local/share/ca-certificates/cacert.org http://www.cacert.org/certs/root.crt http://www.cacert.org/certs/class3.crt
RUN update-ca-certificates
RUN git config --global http.sslCAinfo /etc/ssl/certs/ca-certificates.crt

RUN git clone https://github.com/kb7ky/libcsp.git

WORKDIR libcsp

RUN python3 ./waf distclean configure build --with-os=posix --enable-rdp --enable-promisc --enable-crc32 --enable-hmac --enable-dedup --with-driver-usart=linux --enable-if-zmqhub --enable-examples --enable-python3-bindings --enable-can-socketcan --with-driver-usart=linux --enable-output --enable-print-file

EXPOSE 6000/tcp
EXPOSE 7000/tcp
EXPOSE 6001/udp
EXPOSE 6002/udp