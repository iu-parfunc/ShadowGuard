
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
	"jit_avxv2.cc",
	"jit_avxv3.cc",
	"jit_avx512.cc",
	"jit_memory.cc",
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
    name = "stack_depth",
    srcs = [
        "parse.cc",
        "parse.h",
        "stack_depth.cc",
    ],
    deps = [
        "@com_github_gflags_gflags//:gflags",
        "@glog//:glog",
       	"@dyninst//:dyninst",
    ],
)

cc_binary(
    name = "stack_protector",
    srcs = [
	"parse.cc",
	"parse.h",
        "stack_protector.cc",
	"utils.cc",
	"utils.h",
    ],
    deps = [
        "@com_github_gflags_gflags//:gflags",
        "@glog//:glog",
       	"@dyninst//:dyninst",
    ],
)

cc_binary(
    name = "stack_writes",
    srcs = [
        "stack_writes.cc",
    ],
    deps = [
       	"@dyninst//:dyninst",
    ],
)


cc_library(
    name = "depth",
    srcs = [
        "depth.cc",
    ],
)
