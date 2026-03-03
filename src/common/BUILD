load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "status_macros",
    hdrs = ["status_macros.h"],
    copts = ["-std=c++23"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "execution_cache",
    srcs = ["execution_cache.cpp"],
    hdrs = ["execution_cache.h"],
    copts = ["-std=c++23"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
    visibility = ["//visibility:public"],
)
