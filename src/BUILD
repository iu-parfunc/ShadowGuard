
cc_binary(
    name = "cfi",
    srcs = [
        "cfi.cc",
	"instrument.cc",
	"instrument.h",
        "jit.cc",
	"jit.h",
	"jit_avx2.cc",
	"jit_internal.h",
        "register_usage.cc",
        "register_usage.h",
	"register_utils.cc",
	"register_utils.h",
	"utils.cc",
	"utils.h",
    ],
    deps = [
       	"@asmjit//:asmjit",
        "@com_github_gflags_gflags//:gflags",
       	"@dyninst//:dyninst",
        "@glog//:glog",
    ],
    copts = [
        "-stdlib=libstdc++",
    ],
)

cc_binary(
    name = "sizeof",
    srcs = [
        "sizeof.cc",
    ],
    deps = [
        "@asmjit//:asmjit",
        "@com_github_gflags_gflags//:gflags",
        "@dyninst//:dyninst",
        "@glog//:glog",
    ],
)
