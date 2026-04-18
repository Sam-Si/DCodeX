load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

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
    name = "execution_step_decorator",
    srcs = ["execution_step_decorator.cpp"],
    hdrs = ["execution_step_decorator.h"],
    copts = ["-std=c++23"],
    deps = [
        ":execution_step",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
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
    name = "execution_pipeline_builder",
    srcs = ["execution_pipeline_builder.cpp"],
    hdrs = ["execution_pipeline_builder.h"],
    copts = ["-std=c++23"],
    deps = [
        ":execution_pipeline",
        ":execution_step",
        "//src/common:execution_cache",
        "@com_google_absl//absl/strings",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "language_toolchain",
    srcs = ["language_toolchain.cpp"],
    hdrs = ["language_toolchain.h"],
    copts = ["-std=c++23"],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
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
        ":language_toolchain",
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
        ":execution_pipeline_builder",
        ":execution_step",
        ":execution_strategy",
        ":execution_types",
        ":language_toolchain",
        ":process_timeout_manager",
        "//src/common:execution_cache",
        "//src/common:status_macros",
        "//proto:sandbox_cc_grpc",
        "@com_github_grpc_grpc//:grpc++_unsecure",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
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
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "dynamic_worker_coordinator",
    srcs = ["dynamic_worker_coordinator.cpp"],
    hdrs = ["dynamic_worker_coordinator.h"],
    copts = ["-std=c++23"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
    visibility = ["//visibility:public"],
)

# Integration tests for sandboxed process execution.
# Linux-specific tests (RLIMIT_CPU, RLIMIT_AS enforcement) are gated with
# #ifdef __linux__ inside the test file; they run cleanly on macOS as no-ops.

cc_test(
    name = "warm_worker_pool_test",
    srcs = ["warm_worker_pool_test.cc"],
    copts = ["-std=c++23"],
    linkstatic = True,
    deps = [
        ":warm_worker_pool",
        "@com_google_absl//absl/synchronization",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "dynamic_worker_coordinator_test",
    srcs = ["dynamic_worker_coordinator_test.cc"],
    copts = ["-std=c++23"],
    linkstatic = True,
    deps = [
        ":dynamic_worker_coordinator",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sandbox_test",
    srcs = ["sandbox_test.cc"],
    copts = ["-std=c++23"],
    # Tests that exercise real subprocesses need longer timeouts.
    # --test_timeout=120 covers CPU-burn tests that run for ~5 seconds each.
    timeout = "moderate",
    linkstatic = True,
    deps = [
        ":sandbox",
        ":dynamic_worker_coordinator",
        "//src/common:execution_cache",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

test_suite(
    name = "concurrency_tests",
    tests = [
        ":warm_worker_pool_test",
        ":dynamic_worker_coordinator_test",
        ":sandbox_test",
    ],
)
