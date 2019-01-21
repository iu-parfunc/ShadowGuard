
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
  mkdir build;\
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
  mkdir build;\
  cd build

  # Configure
  cmake -DASMJIT_STATIC=ON -DASMJIT_BUILD_X86=ON -DCMAKE_INSTALL_PREFIX=`pwd`/../install -G 'Unix Makefiles' ..

  # Build
  nprocs=`cat /proc/cpuinfo | awk '/^processor/{print $3}' | wc -l`
  make -j "$(($nprocs / 2))"

  # Install
  make install
fi
