#!/bin/bash

build () {
  pkey=`cat $1`

  # Remove previous intermediate untagged builds .
  docker images -a |grep "<none>"|awk '{print $3}'|xargs docker rmi -f
  # Remove previous tagged builds .
  docker rmi -f `whoami`/shadow_guard

  # Build the docker image.
  docker build --file Dockerfile --build-arg SSH_PRIVATE_KEY="$pkey" -t `whoami`/shadow_guard .
}

run () {
  mount=$1
  bench=$2
  size=$3

  # Run the docker image with a specified bound mount.
  docker run --name=`whoami`_shadow_guard --rm --privileged --userns=host --cap-add=SYS_ADMIN --cap-add=SYS_PTRACE -v "$mount":/home/results -e BENCH="$bench" -e SIZE="$size" `whoami`/shadow_guard
}

POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

MOUNT=`pwd`/"results"
BENCH="intspeed"
PKEY="id_rsa"
SIZE="test"

case $key in
    build)
    ACTION="build"
    shift # past value
    ;;
    run)
    ACTION="run"
    shift # past value
    ;;
    -b|--bench)
    BENCH="$2"
    shift # past argument
    shift # past value
    ;;
    -k|--key)
    PKEY="$2"
    shift # past argument
    shift # past value
    ;;
    -m|--mount)
    MOUNT="$2"
    shift # past argument
    shift # past value
    -s|--size)
    SIZE="$2"
    shift # past argument
    shift # past value
    ;;
    *)    # unknown option
    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done

set -- "${POSITIONAL[@]}" # restore positional parameters

if ! [ -z "$ACTION" ];then
  if [ "$ACTION" == "build" ]; then
    build $PKEY  
  elif [ "$ACTION" == "run" ]; then
    run $MOUNT $BENCH $SIZE
  else
    echo "Unknown action $ACTION" 2>&1
  fi
else
  echo "No action provided. Valid options are build and run." 2>&1
fi
