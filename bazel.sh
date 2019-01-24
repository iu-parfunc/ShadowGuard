
deps () {
  ### Build Dyninst

  # Fetch
  if [ ! -d "thirdparty/dyninst-10.0.0" ]; then
    wget https://github.com/dyninst/dyninst/archive/v10.0.0.tar.gz;\
    tar -xzvf v10.0.0.tar.gz -C thirdparty/
    rm v10.0.0.tar.gz;\
  fi

  if [ ! -d "thirdparty/dyninst-10.0.0/install" ]; then
    cd thirdparty/dyninst-10.0.0/;\
    mkdir install;\
    mkdir -p build;\
    cd build

    # Configure
    cmake -DPATH_BOOST= -DLIBELF_INCLUDE_DIR= -DLIBELF_INCLUDE_DIR= -DLIBDWARF_INCLUDE_DIR= -DLIBDWARF_LIBRARIES= -DCMAKE_INSTALL_PREFIX=`pwd`/../install -G 'Unix Makefiles' ..

    # Build
    #   Dyninst build tends to succeed with a retry after an initial build failure.
    #   Cover that base with couple of retries.
    nprocs=`cat /proc/cpuinfo | awk '/^processor/{print $3}' | wc -l`
    make -j "$(($nprocs / 2))"
    for i in 1 2 3; do
      if [ $? -eq 0 ]; then
        break
      fi
      make -j
    done
  
    # Install
    make install
  fi


  ### Build Asmjit

  # Fetch

  if [ ! -d "thirdparty/asmjit" ]; then
    git clone https://github.com/asmjit/asmjit.git thirdparty/asmjit
  fi

  if [ ! -d "thirdparty/asmjit/install" ]; then
    cd thirdparty/asmjit;\
    mkdir install;\
    mkdir -p build;\
    cd build

    # Configure
    cmake -DASMJIT_STATIC=ON -DASMJIT_BUILD_X86=ON -DCMAKE_INSTALL_PREFIX=`pwd`/../install -G 'Unix Makefiles' ..

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

clean () {
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
    install)
    ACTION="install"
    shift # past value
    ;;
    clean)
    ACTION="clean"
    shift # past value
    ;;
    -m|--mode)
    MODE="$2"
    shift # past argument
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
  elif [ "$ACTION" == "install" ]; then
    install $PREFIX
  elif [ "$ACTION" == "clean" ]; then
    clean
  else
    echo "Unknown action $ACTION" >> /dev/stderr
  fi
else
  build $MODE
fi
