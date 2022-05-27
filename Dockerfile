# Dockerfile for libcsp and friends
# this file is to setup the dev environment
FROM ubuntu:20.04 AS builder-image

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y  --no-install-recommends build-essential git python3 wget ca-certificates apt-transport-https pkg-config libczmq-dev python3-dev libsocketcan-dev libyaml-dev socat python3-pip python3-tk iputils-ping iproute2 net-tools netcat-traditional expect-dev
RUN pip install zmq tk

# get certs so https to github will work
RUN mkdir -p /usr/local/share/ca-certificates/cacert.org
RUN wget -P /usr/local/share/ca-certificates/cacert.org http://www.cacert.org/certs/root.crt http://www.cacert.org/certs/class3.crt
RUN update-ca-certificates
RUN git config --global http.sslCAinfo /etc/ssl/certs/ca-certificates.crt

