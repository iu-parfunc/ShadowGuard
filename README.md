# liteCFI
Lightweight backward-edge CFI using a low overhead shadow stack.

# Installation

We use Bazel for building this project. Get it from https://bazel.build/. 
Since we depend on Dyninst and Asmjit which have cmake builds, a recent 
cmake installation is also required for building those dependencies. Use the 
Bazel build wrapper script as below to build the project along with its 
dependencies.

Below command fetches and builds the dependecies.
$ ./bazel.sh deps 

Run below command to build the project
$ ./bazel.sh build [options]

Current build options include --debug which will build a debug binary and 
--release which will build a release binary. A build with no option will build 
a debug binary by default.

Additionally you can run `./bazel.sh clean` to clean up build artifacts.

