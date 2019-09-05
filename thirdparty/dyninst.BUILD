cc_library(
    name = "dyninst",
    srcs = glob(["install/lib/*.so"]), 
    hdrs = glob(["install/include/*.h"]) + glob(["install/include/elfutils/*.h"]) + 
        glob(["install/include/tbb/**/*.h"]) +
        glob(["install/include/boost/**/*.*"]),
    includes = ["install/include", "install/include/elfutils",
         "install/include/tbb/", "install/include/boost"],
    linkopts = ["-lboost_system"],
    visibility = ["//visibility:public"],
)
