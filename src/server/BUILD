load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "execution_cache",
    srcs = ["execution_cache.cpp"],
    hdrs = ["execution_cache.h"],
    copts = ["-std=c++23"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "sandbox",
    srcs = ["sandbox.cpp"],
    hdrs = ["sandbox.h", "output_filter.h", "process_runner.h", "temp_file_manager.h"],
    copts = ["-std=c++23"],
    deps = [
        ":execution_cache",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "server",
    srcs = ["main.cpp"],
    copts = ["-std=c++23"],
    deps = [
        ":sandbox",
        ":execution_cache",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
    ],
)