load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "sandbox_proto",
    srcs = ["sandbox.proto"],
)

cc_proto_library(
    name = "sandbox_cc_proto",
    deps = [":sandbox_proto"],
)

cc_grpc_library(
    name = "sandbox_cc_grpc",
    srcs = [":sandbox_proto"],
    grpc_only = True,
    deps = [":sandbox_cc_proto"],
)
