#!/bin/bash

bench=$1

dyninst_lib=/home/buddhika/Builds/retguard/thirdparty/dyninst-10.1.0/install/lib

cp $bench $bench.orig

OMP_NUM_THREADS=1 LD_LIBRARY_PATH=$dyninst_lib:$LD_LIBRARY_PATH DYNINSTAPI_RT_LIB=$dyninst_lib/libdyninstAPI_RT.so /home/buddhika/Builds/retguard/bazel-bin/src/cfi --shadow_stack=light --stats=ANALYSIS.txt --vv --output=$bench $bench
