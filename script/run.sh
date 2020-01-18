#!/bin/bash

bench=$*

root=$LITECFI_HOME

dyninst_lib=$root/thirdparty/dyninst-10.1.0/install/lib

LD_PRELOAD=$root/bazel-bin/src/libstackrt.so:$root/thirdparty/dyninst-10.1.0/install/lib/libdyninstAPI_RT.so $bench
