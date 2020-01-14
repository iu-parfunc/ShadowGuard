#!/bin/bash

bench=$1

dyninst_lib=/home/buddhika/Builds/retguard/thirdparty/dyninst-10.1.0/install/lib

LD_PRELOAD=/home/buddhika/Builds/retguard/bazel-bin/src/libstackrt.so:/home/buddhika/Builds/retguard/thirdparty/dyninst-10.1.0/install/lib/libdyninstAPI_RT.so $bench
