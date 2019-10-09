cc_binary(
    name = "safe_leaf",
    srcs = [ "safe_leaf.cc"],
    copts = [
	"-O0",
	"-fno-stack-protector",
    ],
)

cc_binary(
    name = "safe_non_leaf",
    srcs = [ "safe_non_leaf.cc"],
    copts = [
	"-O0",
	"-fno-stack-protector",
    ],
)

cc_binary(
    name = "unsafe_leaf",
    srcs = [ "unsafe_leaf.cc"],
    copts = [
	"-O0",
	"-fno-stack-protector",
    ],
)

cc_binary(
    name = "unsafe_non_leaf",
    srcs = [ "unsafe_non_leaf.cc"],
    copts = [
	"-O0",
	"-fno-stack-protector",
    ],
)

cc_binary(
    name = "indirect_call",
    srcs = [ "indirect_call.cc"],
    copts = [
	"-O0",
	"-fno-stack-protector",
    ],
)

cc_binary(
    name = "indirect_call_tree",
    srcs = [ "indirect_call_tree.cc"],
    copts = [
	"-O0",
	"-fno-stack-protector",
    ],
)

cc_binary(
    name = "plt_call",
    srcs = [ "plt_call.cc"],
    copts = [
	"-O0",
	"-fno-stack-protector",
    ],
)

cc_binary(
    name = "plt_call_tree",
    srcs = [ "plt_call_tree.cc"],
    copts = [
	"-O0",
	"-fno-stack-protector",
    ],
)

cc_binary(
    name = "analysis_test",
    srcs = [ 
	"analysis_test.cc",
    ],
    deps = [
        "//src:analysis",
        "//tests:safe_leaf",
        "//tests:safe_non_leaf",
        "//tests:unsafe_leaf",
        "//tests:unsafe_non_leaf",
        "//tests:indirect_call",
        "@glog//:glog",
        "@com_github_gflags_gflags//:gflags",
        "@gtest//:gtest_main",
       	"@dyninst//:dyninst",
    ],
    copts = [
	"-O0",
	"-faligned-new",
	"-fno-stack-protector",
    ],
)
