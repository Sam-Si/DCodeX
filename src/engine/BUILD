load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "execution_types",
    hdrs = ["execution_types.h"],
    copts = ["-std=c++23"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "execution_step",
    hdrs = ["execution_step.h"],
    copts = ["-std=c++23"],
    deps = [
        ":execution_types",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "execution_pipeline",
    hdrs = ["execution_pipeline.h"],
    copts = ["-std=c++23"],
    deps = [
        ":execution_step",
        ":execution_types",
        "//src/common:execution_cache",
        "@com_google_absl//absl/status:statusor",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "execution_strategy",
    hdrs = ["execution_strategy.h"],
    copts = ["-std=c++23"],
    deps = [
        ":execution_pipeline",
        ":execution_types",
        "//src/common:execution_cache",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "process_timeout_manager",
    hdrs = ["process_timeout_manager.h"],
    copts = ["-std=c++23"],
    deps = [
        "@com_github_grpc_grpc//:grpc++_unsecure",
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
        ":execution_pipeline",
        ":execution_step",
        ":execution_strategy",
        ":execution_types",
        ":process_timeout_manager",
        "//src/common:execution_cache",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++_unsecure",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_macros",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "warm_worker_pool",
    srcs = ["warm_worker_pool.cpp"],
    hdrs = ["warm_worker_pool.h"],
    copts = ["-std=c++23"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
    ],
    visibility = ["//visibility:public"],
)
