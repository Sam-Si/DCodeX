load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "server",
    srcs = ["main.cpp", "sandbox.cpp", "sandbox.h", "process.cpp", "process.h", "logger.h"],
    copts = ["-std=c++17"],
    deps = [
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++",
    ],
)
