
cc_binary(
    name = "cfi",
    srcs = [
        "cfi.cc",
	"instrument.cc",
	"instrument.h",
        "jit.cc",
	"jit.h",
        "register_usage.cc",
        "register_usage.h",
    ],
    deps = [
       	"@asmjit//:asmjit",
        "@com_github_gflags_gflags//:gflags",
       	"@dyninst//:dyninst",
        "@glog//:glog",
    ],
)
