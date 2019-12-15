#!/bin/bash

deps () {
  ### Build Capstone  
  root_dir=`pwd`
  # Fetch
  if [ ! -d "thirdparty/capstone" ]; then
    git clone https://github.com/mxz297/capstone.git thirdparty/capstone
    cd thirdparty/capstone/;\
    git checkout access-fixes
  fi

  cd $root_dir

  if [ ! -d "thirdparty/capstone/install" ]; then
    cd thirdparty/capstone/;\
    mkdir install;\
    mkdir -p build;\
    cd build

    # Configure
    cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../install ..

    # Install
    nprocs=`cat /proc/cpuinfo | awk '/^processor/{print $3}' | wc -l`
    make -j "$(($nprocs / 2))" install
  fi

  ### Build libunwind
  cd $root_dir
  if [ ! -d "thirdparty/libunwind" ]; then
    git clone  https://github.com/mxz297/libunwind.git thirdparty/libunwind
  fi

  if [ ! -d "thirdparty/libunwind/install" ]; then
    cd thirdparty/libunwind/;\
    mkdir install;\

    # Configure
    ./autogen.sh
    ./configure --prefix=`pwd`/install --enable-cxx-exceptions

    # Install
    nprocs=`cat /proc/cpuinfo | awk '/^processor/{print $3}' | wc -l`
    make -j "$(($nprocs / 2))" install
  fi

  ### Build Dyninst
  cd $root_dir

  # Fetch
  if [ ! -d "thirdparty/dyninst-10.1.0" ]; then
    git clone https://github.com/mxz297/dyninst.git thirdparty/dyninst-10.1.0
    cd thirdparty/dyninst-10.1.0/;\
    git checkout liteCFI
  fi

  cd $root_dir

  if [ ! -d "thirdparty/dyninst-10.1.0/install" ]; then
    cd thirdparty/dyninst-10.1.0/;\
    mkdir install;\
    mkdir -p build;\
    cd build

    # Configure
    cmake -DLibunwind_ROOT_DIR=`pwd`/../../libunwind/install -DCapstone_ROOT_DIR=`pwd`/../../capstone/install/ -DCMAKE_INSTALL_PREFIX=`pwd`/../install -G 'Unix Makefiles' ..

    # Build
    #   Dyninst build tends to succeed with a retry after an initial build failure.
    #   Cover that base with couple of retries.
    nprocs=`cat /proc/cpuinfo | awk '/^processor/{print $3}' | wc -l`
    make -j "$(($nprocs / 2))"
    for i in 1 2 3; do
      if [ $? -eq 0 ]; then
        break
      fi
      make -j "$(($nprocs / 2))"
    done
  
    # Install
    make install
  fi


  ### Build Asmjit

  # Fetch

  cd $root_dir
  if [ ! -d "thirdparty/asmjit" ]; then
    git clone https://github.com/asmjit/asmjit.git thirdparty/asmjit
    # git clone -b next-wip --single-branch https://github.com/asmjit/asmjit.git thirdparty/asmjit
  fi

  if [ ! -d "thirdparty/asmjit/install" ]; then
    cd thirdparty/asmjit;\
    mkdir install;\
    mkdir -p build;\
    cd build

    # Configure
    cmake -E env CXXFLAGS="-fPIC" cmake -DASMJIT_STATIC=ON -DASMJIT_BUILD_X86=ON -DCMAKE_INSTALL_PREFIX=`pwd`/../install -G 'Unix Makefiles' ..

    # Build
    nprocs=`cat /proc/cpuinfo | awk '/^processor/{print $3}' | wc -l`
    make -j "$(($nprocs / 2))"

    # Install
    make install
  fi
}

build () {
  if ! [ -z "$1" ]; then
    if [ "$1" == "debug" ]; then
       bazel build -c dbg --sandbox_debug //src:* 
    elif [ "$1" == "release" ]; then
       bazel build //src:*
    else 
       echo "Unknown build option $1" >> /dev/stderr
    fi 
  else
    bazel build -c dbg --sandbox_debug //src:* 
  fi
}

run_tests() {
  cd tests; bazel clean; bazel test -c dbg //tests:*
  cd ..;LD_LIBRARY_PATH=`pwd`/thirdparty/dyninst-10.1.0/install/lib/:$LD_LIBRARY_PATH ./bazel-bin/tests/analysis_test
}

clean () {
  bazel clean
}

realclean () {
  rm -rf thirdparty/asmjit
  rm -rf thirdparty/dyninst-10.1.0
  bazel clean
}

POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    deps)
    ACTION="deps"
    shift # past value
    ;;
    build)
    ACTION="build"
    shift # past value
    ;;
    test)
    ACTION="test"
    shift # past value
    ;;
    install)
    ACTION="install"
    shift # past value
    ;;
    clean)
    ACTION="clean"
    shift # past value
    ;;
    realclean)
    ACTION="realclean"
    shift # past value
    ;;
    --release)
    MODE="release"
    shift # past value
    ;;
    --debug)
    MODE="debug"
    shift # past value
    ;;
    -p|--prefix)
    PREFIX="$2"
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
  if [ "$ACTION" == "deps" ]; then
    deps
  elif [ "$ACTION" == "build" ]; then
    build $MODE
  elif [ "$ACTION" == "test" ]; then
    run_tests
  elif [ "$ACTION" == "install" ]; then
    install $PREFIX
  elif [ "$ACTION" == "clean" ]; then
    clean
  elif [ "$ACTION" == "realclean" ]; then
    realclean
  else
    echo "Unknown action $ACTION" >> /dev/stderr
  fi
else
  build $MODE
fi
