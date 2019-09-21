cc_binary(
    name = "simple_leaf",
    srcs = [ "simple_leaf.cc"],
)

cc_binary(
    name = "calls_non_leaf",
    srcs = [ "calls_non_leaf.cc"],
)

cc_binary(
    name = "analysis_test",
    srcs = [ 
	"analysis_test.cc",
    ],
    deps = [
	"//src:analysis",
        "//tests:simple_leaf",
        "@com_github_gflags_gflags//:gflags",
        "@gtest//:gtest_main",
       	"@dyninst//:dyninst",
    ],
    copts = [
	"-O0",
	"-faligned-new",
    ],
)
