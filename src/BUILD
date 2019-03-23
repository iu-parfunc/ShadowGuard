
cc_binary(
    name = "cfi",
    srcs = [
        "cache.cc",
	"cache.h",
	"call_graph.h",
        "call_graph_impl.h",
        "cfi.cc",
	"codegen.cc",
	"codegen.h",
	"instrument.cc",
	"instrument.h",
        "jit.cc",
	"jit.h",
	"jit_avx2.cc",
	"jit_avxv2.cc",
	"jit_avxv3.cc",
	"jit_avx512.cc",
	"jit_internal.h",
	"jit_nop.cc",
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
	"//src:tls",
    ],
)

cc_library(
    name = "tls",
    srcs = [
        "thread_local.c",
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

cc_binary(
    name = "reg_test",
    srcs = [
        "reg_test.cc",
	"parse.cc",
	"parse.h",
    ],
    deps = [
        "@dyninst//:dyninst",
    ],
)

cc_binary(
    name = "icall_test",
    srcs = [
        "icall_test.cc",
	"parse.cc",
	"parse.h",
    ],
    deps = [
        "@dyninst//:dyninst",
    ],
)

cc_binary(
    name = "icall",
    srcs = [
        "icall.cc",
    ],
)
