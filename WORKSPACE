load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

git_repository(
    name = "absl",
    remote = "https://github.com/abseil/abseil-cpp.git",
    branch = "master",
)

git_repository(
    name = "gtest",
    remote = "https://github.com/abseil/googletest.git",
    tag = "release-1.8.1",
)

git_repository(
    name = "com_github_gflags_gflags",
    remote = "https://github.com/gflags/gflags.git",
    tag = "v2.2.2"
)

git_repository(
    name = "glog",
    remote = "https://github.com/google/glog.git",
    branch = "master"
)

'''
new_local_repository(
    name = "dyninst",
    path = "/home/buddhika/Builds/liteCFI/thirdparty",
    build_file = "thirdparty/BUILD.dyninst",
)

load("//thirdparty:dyninst.bzl", "dyninst_repository")

dyninst_repository(
    name = "dyninst",
)
'''

new_local_repository (
    name = "dyninst",
    path = "thirdparty/dyninst-10.0.0",
    build_file = '@//thirdparty:dyninst.BUILD',
)
