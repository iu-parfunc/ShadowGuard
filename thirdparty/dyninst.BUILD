cc_library(
    name = "dyninst",
    srcs = glob(["install/lib/*.so"]) + glob(["build/elfutils/lib/*.so"]) + 
        glob(["build/elfutils/lib/elfutils/*.so"]) + glob(["build/tbb/lib/*.*"]),
    hdrs = glob(["install/include/*.h"]) + glob(["build/elfutils/include/*.h"]) + 
        glob(["build/elfutils/include/elfutils/*.h"]) + glob(["build/tbb/include/**/*.h"]),
    includes = ["install/include", "build/elfutils/include/elfutils/",
         "build/elfutils/include/", "build/tbb/include/"],
    linkopts = ["-L/usr/local/lib/ -lboost_system"],
    visibility = ["//visibility:public"],
)
