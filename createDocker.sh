#!/bin/bash
#
# create docker images csp_bridge zmqproxy and libcspbuild
docker build -t cspbridge .
docker build -t zmqproxy -f Dockerfile-zmqproxy .
docker build -t cspbridge -f Dockerfile-cspbridge .