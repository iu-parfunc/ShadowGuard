
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
  cmake -DPATH_BOOST= -DLIBELF_INCLUDE_DIR= -DLIBELF_INCLUDE_DIR= -DLIBDWARF_INCLUDE_DIR= -DLIBDWARF_LIBRARIES= -DCMAKE_INSTALL_PREFIX=`pwd`/../install -DENABLE_STATIC_LIBS=TRUE -G 'Unix Makefiles' ..

  # Build
  #   Dyninst build tends to succeed with a retry after an initial build failure.
  #   Cover that base with couple of retries.
  nprocs=grep ^cpu\\scores /proc/cpuinfo | uniq |  awk '{print $4}'
  echo "$(($nprocs / 2))"
  make -j $(($nprocs / 2))
  for i in 1 2 3; do
    if [ $? -eq 0 ]; then
      break
    fi
    make -j
  done
  
  # Install
  make install
fi
