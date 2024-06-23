#ifndef FILE_SERVER_FILE_SERVER_H
#define FILE_SERVER_FILE_SERVER_H

#include <string>
#include <cstdint>

#include "coke/http_server.h"

struct FileServerParams {
    std::size_t min_size_hint{256 * 1024};
    std::string root{"."};
};

class HttpFileServer : public coke::HttpServer {
    static auto get_proc(HttpFileServer *server) {
        return [server](coke::HttpServerContext ctx) {
            return server->process(std::move(ctx));
        };
    }

public:
    HttpFileServer(const coke::HttpServerParams &hparams,
                   const FileServerParams &fparams)
        : coke::HttpServer(hparams, get_proc(this)), params(fparams)
    { }

private:
    coke::Task<> process(coke::HttpServerContext ctx);

    coke::Task<> handle_get(coke::HttpServerContext &ctx);

    coke::Task<> handle_head(coke::HttpServerContext &ctx);

    coke::Task<> handle_other(coke::HttpServerContext &ctx,
                              const std::string &method);

private:
    FileServerParams params;
};

#endif // FILE_SERVER_FILE_SERVER_H
