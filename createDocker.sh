#!/bin/bash
#
# create docker images csp_bridge zmqproxy and libcspbuild

VERSION=0.0.1
export AWS_REGION="us-east-1"
export AWS_ACCOUNT_ID=100311576175
AWSCREDS="dtk"
export CONTAINER_VERSION="latest"
LOCALONLY=true
ECRPREFIX=kb7ky/infra-build

usage() {
    echo "$0: [ -r ] [ -v version ]" 1>&2
    echo "version: ${VERSION}"
}

while getopts "rv:" options
do
    case "${options}" in
    r)
        LOCALONLY="false";
        ;;
    v)
        CONTAINER_VERSION=$OPTARG
        ;;
    *)
        usage
        exit 1
        ;;
    esac 
done

docker build -t cspdev .
docker build --no-cache -t csp-base-stdio -f Dockerfile-csp-base-stdio .
docker build --no-cache -t csp-base-file -f Dockerfile-csp-base-file .
docker build --no-cache -t csp-base-none -f Dockerfile-csp-base-none .
docker build -t zmqproxy -f Dockerfile-zmqproxy .
docker build -t cspbridge -f Dockerfile-cspbridge .
docker build -t ${ECRPREFIX}/testapps:${CONTAINER_VERSION} -f Dockerfile-test .

if [ "${LOCALONLY}" != "true" ]
then
  export AWS_ECR_REPO="${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/"

  aws ecr get-login-password --region ${AWS_REGION} | docker login --username AWS --password-stdin ${AWS_ECR_REPO}
  for imagename in testapps
  do
    echo "Pushing ${imagename}"
    docker tag ${ECRPREFIX}/${imagename}:${CONTAINER_VERSION} "${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECRPREFIX}/${imagename}:${CONTAINER_VERSION}"
    docker push "${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECRPREFIX}/${imagename}:${CONTAINER_VERSION}"
  done
fi
