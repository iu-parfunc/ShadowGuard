cc_binary(
    name = "simple_leaf",
    srcs = [ "simple_leaf.cc"],
)

cc_binary(
    name = "analysis_test",
    srcs = [ 
	"analysis_test.cc",
    ],
    deps = [
	"//src:analysis",
        "//tests:simple_leaf",
        "@gtest//:gtest_main",
       	"@dyninst//:dyninst",
    ],
    copts = [
	"-faligned-new",
    ],
)
