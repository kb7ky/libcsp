#!/bin/bash
#
# create docker images csp_bridge zmqproxy and libcspbuild
docker build -t cspdev .
docker build --no-cache -t csp-base-stdio -f Dockerfile-csp-base-stdio .
docker build --no-cache -t csp-base-file -f Dockerfile-csp-base-file .
docker build --no-cache -t csp-base-none -f Dockerfile-csp-base-none .
docker build -t zmqproxy -f Dockerfile-zmqproxy .
docker build -t cspbridge -f Dockerfile-cspbridge .
docker build -t cspapps -f Dockerfile-test .
