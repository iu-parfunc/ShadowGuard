cc_binary(
    name = "safe_leaf",
    srcs = [ "safe_leaf.cc"],
)

cc_binary(
    name = "safe_non_leaf",
    srcs = [ "safe_non_leaf.cc"],
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
        "@com_github_gflags_gflags//:gflags",
        "@gtest//:gtest_main",
       	"@dyninst//:dyninst",
    ],
    copts = [
	"-O0",
	"-faligned-new",
    ],
)
