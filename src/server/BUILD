load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "execution_cache",
    srcs = ["execution_cache.cpp"],
    hdrs = ["execution_cache.h"],
    linkopts = ["-lcrypto"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "sandbox",
    srcs = ["sandbox.cpp"],
    hdrs = ["sandbox.h"],
    deps = [
        ":execution_cache",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++",
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "server",
    srcs = ["main.cpp"],
    deps = [
        ":sandbox",
        ":execution_cache",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++",
    ],
)