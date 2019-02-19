
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
)

cc_library(
    name = "tls",
    srcs = [
        "thread_local.cc",
        "thread_local.h",
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
