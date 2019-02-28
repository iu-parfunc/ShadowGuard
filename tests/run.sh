################### Configs (Edit) ####################

declare -A tests

# Add new tests here
#
# FORMAT : test[$test_name]='$test_source cfi_flags...'
#
tests["basic test"]='basic_test.cc --instrument=shared'
tests["overflow test"]='overflow_test.cc --instrument=shared'
tests["spill test"]='spill_test.cc --instrument=shared'
tests["spill and overflow"]='spill_and_overflow_test.cc --instrument=shared'

#######################################################

################### Script (Do not edit) ##############

ROOT=`pwd`/..
CFI=./../bazel-out/k8-dbg/bin/src/cfi 

cleanup() {
  if ! [ -z "$1" ]; then
    rm -f $1
  fi 
  rm -f *.so
  rm -f *_cfi
}

check() {
if [ $? -ne 0 ]; then
    echo -e "$1 failed"
    cleanup $2
    exit 1
fi
}

run() {
  if ! [ -z "$1" ]; then
    test_binary=${1%.*}
    # Compile
    echo -e "Compile : gcc -o $test_binary $1\n"
    gcc -o $test_binary $1

    check "Compilation" $test_binary

    # Harden
    echo -e "Harden  : $CFI $test_binary $2"
    $CFI $test_binary $2

    check "Hardening " $test_binary

    echo -e "\nRun     : ./$test_binary""_cfi"
    echo ""
    ./$test_binary"_cfi"

    check "Run " $test_binary

    rm -f test_binary
  else
    echo "No test application provided." >> /dev/stderr
  fi
}

run_tests() {
  export DYNINSTAPI_RT_LIB=../thirdparty/dyninst-10.0.0/install/lib/libdyninstAPI_RT.so
  if [ -f libtls.so ]; then
    rm -f libtls.so
  fi
  cp $ROOT/bazel-out/k8-dbg/bin/src/libtls.so .

  for i in "${!tests[@]}"
  do
    test_name=$i
    opts=(${tests[$i]})
    test_source=${opts[0]}
    flags=${opts[@]:1}

    echo -e "\n-------------- Running $test_name --------------\n"

    run $test_source "${flags[@]}" 
  done

  cleanup
}

run_tests
