
cc_binary(
    name = "cfi",
    srcs = [
        "cfi.cc",
	"codegen.cc",
	"codegen.h",
	"instrument.cc",
	"instrument.h",
        "jit.cc",
	"jit.h",
	"jit_avx2.cc",
	"jit_avx512.cc",
	"jit_internal.h",
	"parse.cc",
	"parse.h",
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
