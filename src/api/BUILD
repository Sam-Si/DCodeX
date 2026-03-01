load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "execute_reactor",
    srcs = ["execute_reactor.cpp"],
    hdrs = ["execute_reactor.h"],
    copts = ["-std=c++23"],
    deps = [
        "//src/engine:sandbox",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++_unsecure",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "code_executor_service",
    srcs = ["code_executor_service.cpp"],
    hdrs = ["code_executor_service.h"],
    copts = ["-std=c++23"],
    deps = [
        ":execute_reactor",
        "//src/engine:warm_worker_pool",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++_unsecure",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "server",
    srcs = ["main.cpp"],
    copts = ["-std=c++23"],
    deps = [
        ":code_executor_service",
        "@com_github_grpc_grpc//:grpc++_unsecure",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
    visibility = ["//visibility:public"],
)
