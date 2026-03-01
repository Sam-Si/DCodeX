load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "server",
    srcs = ["main.cpp"],
    copts = ["-std=c++23"],
    deps = [
        "//src/engine:sandbox",
        "//src/common:execution_cache",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++_unsecure",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
    ],
    visibility = ["//visibility:public"],
)
