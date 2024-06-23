package(default_visibility = ["//visibility:public"])

cc_library(
    name = "log",
    srcs = [
        "src/log.cpp",
    ],
    hdrs = [
        "include/log/log.h",
        "include/log/ostream_logger.h",
        "include/log/fileno_logger.h",
    ],
    includes = ["include"],
)

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
        "//:log",
        "@coke//:http",
    ]
)

cc_binary(
    name = "http_file_server",
    srcs = ["src/main.cpp"],
    deps = [
        "//:log",
        "//:file_server",
        "@coke//:tools",
    ]
)
