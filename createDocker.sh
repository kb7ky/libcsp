#!/bin/bash
#
# create docker images csp_bridge zmqproxy and libcspbuild
docker build -t cspdev .
docker build --no-cache -t csp-base-stdio -f Docketfile-csp-base-stdio
docker build --no-cache -t csp-base-file -f Docketfile-csp-base-file
docker build --no-cache -t csp-base-none -f Docketfile-csp-base-none
docker build -t zmqproxy -f Dockerfile-zmqproxy .
docker build -t cspbridge -f Dockerfile-cspbridge .
docker build -t cspapps -f Dockerfile-test .