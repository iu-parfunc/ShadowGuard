cc_library(
    name = "dyninst",
    srcs = glob(["install/lib/*.so"]) + glob(["build/elfutils/lib/*.so"]) + 
        glob(["build/elfutils/lib/elfutils/*.so"]) + glob(["build/tbb/lib/*.*"]) +
        glob(["build/boost/src/boost/stage/lib/*.so"]),
    hdrs = glob(["install/include/*.h"]) + glob(["build/elfutils/include/*.h"]) + 
        glob(["build/elfutils/include/elfutils/*.h"]) + glob(["build/tbb/include/**/*.h"]) +
        glob(["build/boost/src/boost/boost/**/*.*"]),
    includes = ["install/include", "build/elfutils/include/elfutils/",
         "build/elfutils/include/", "build/tbb/include/", "build/boost/src/boost"],
    linkopts = ["-Lbuild/boost/src/boost/stage/lib -lboost_system-mt"],
    visibility = ["//visibility:public"],
)
