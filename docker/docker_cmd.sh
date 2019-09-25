#!/bin/bash

cd ..
./bazel.sh deps
./bazel.sh build --debug
./bazel.sh test
