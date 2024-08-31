package(default_visibility = ["//visibility:public"])

cc_library(
    name = "file_server",
    srcs = [
        "src/file_server.cpp",
    ],
    hdrs = [
        "include/file_server.h",
    ],
    includes = ["include"],
    deps = [
        "@common//:log",
        "@coke//:http",
    ]
)

cc_binary(
    name = "http_file_server",
    srcs = ["src/main.cpp"],
    deps = [
        "//:file_server",
        "@common//:log",
        "@coke//:tools",
    ]
)
