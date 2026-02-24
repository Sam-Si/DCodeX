load("@rules_python//python:defs.bzl", "py_binary", "py_library")

package(default_visibility = ["//visibility:public"])

exports_files(["requirements.txt"])

py_library(
    name = "client_lib",
    srcs = glob(["*.py"]),
    deps = [
        "//proto:sandbox_py_proto",
        "//proto:sandbox_py_grpc",
        "@pip//grpcio",
    ],
)

py_binary(
    name = "main",
    srcs = ["main.py"],
    deps = [
        ":client_lib",
    ],
)
