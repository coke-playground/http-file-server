#include <iostream>
#include <atomic>
#include <csignal>

#include "coke/tools/option_parser.h"

#include "file_server.h"
#include "log/log.h"

std::atomic<bool> run_flag{true};

void sighandler(int) {
    run_flag.store(false, std::memory_order_relaxed);
    run_flag.notify_all();
}

int main(int argc, char *argv[]) {
    coke::HttpServerParams http_params;
    FileServerParams file_server_params;
    int port = 8000;

    coke::OptionParser args;
    args.add_integer(port, 'p', "port").set_default(8000)
        .set_description("The http file server serve on this port");
    args.add_string(file_server_params.root, 'r', "root").set_default(".")
        .set_description("The root directory of the file server");
    args.set_help_flag('h', "help");

    std::string err;
    int ret = args.parse(argc, argv, err);

    if (ret < 0) {
        std::cerr << err << std::endl;
        return 1;
    }
    else if (ret > 0) {
        args.usage(std::cout);
        return 0;
    }

    HttpFileServer server(http_params, file_server_params);

    signal(SIGTERM, sighandler);
    signal(SIGINT, sighandler);

    if (server.start(port) == 0) {
        LOG_INFO("FileServerStart port:{} root:{}", port, file_server_params.root);

        run_flag.wait(true, std::memory_order_relaxed);

        LOG_INFO("FileServerStop shutdown server");
        server.shutdown();

        LOG_INFO("FileServerStop wait finish");
        server.wait_finish();

        LOG_INFO("FileServerStop done");
        return 0;
    }
    else {
        LOG_INFO("FileServerStartFailed port:{} errno:{}", port, (int)errno);
        return 1;
    }
}
