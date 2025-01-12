#include "file_server.h"

#include <chrono>
#include <cctype>
#include <filesystem>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "coke/global.h"
#include "coke/fileio.h"
#include "coke/sleep.h"

#include "klog/klog.h"
#include "workflow/HttpUtil.h"

namespace fs = std::filesystem;

static int64_t current_steady_usec() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto u = std::chrono::duration_cast<std::chrono::microseconds>(now);
    return u.count();
}

static fs::path get_path(coke::HttpRequest &req) {
    std::string uri;

    if (req.get_request_uri(uri)) {
        // 去掉uri中非path的部分
        auto pos = uri.find('?');
        if (pos != std::string::npos)
            uri.erase(pos);

        pos = uri.find('#');
        if (pos != std::string::npos)
            uri.erase(pos);

        if (uri.empty())
            uri.assign("/");
        else if (uri[0] != '/')
            uri = "/" + uri;

        // 正规化路径，避免逃逸到其他目录下
        fs::path path(uri);
        return path.lexically_normal();
    }
    else
        return fs::path("/");
}

static off64_t get_filesize(int fd) {
    struct stat64 s;

    if (fstat64(fd, &s) == 0)
        return s.st_size;

    return -1;
}

static std::string get_http_header(std::size_t size, std::string name) {
    std::string head;

    head.append("HTTP/1.1 200 OK\r\n")
        .append("Connection: Keep-Alive\r\n")
        .append("Content-Length: ").append(std::to_string(size)).append("\r\n")
        .append("Content-Encoding: identity\r\n")
        .append("Content-Disposition: attachment; filename=")
            .append(name).append("\r\n")
        .append("\r\n");

    return head;
}

constexpr std::size_t RBUF_SIZE = 1024 * 1024;

struct FdGuard {
    FdGuard(int fd) : fd(fd) { }

    int release() {
        int ret = fd;
        fd = -1;
        return ret;
    }

    int close() {
        int ret = -1;
        if (fd >= 0)
            ret = ::close(fd);

        fd = -1;
        return ret;
    }

    ~FdGuard() { close(); }

private:
    int fd;
};

// 实现一个伪异步回复逻辑，通过定时器和task->push来回复大量数据
static coke::Task<int>
reply_buf(WFHttpTask *task, const char *buf, std::size_t size) {
    constexpr std::chrono::nanoseconds WAIT_MIN(100LL * 1000);
    constexpr std::chrono::nanoseconds WAIT_MAX(1000LL * 1000 * 1000);

    std::chrono::nanoseconds wait_nsec = WAIT_MIN;
    std::size_t offset = 0;
    int error = 0;
    int ret;

    while (offset < size) {
        ret = task->push(buf + offset, size - offset);
        if (ret < 0 && errno == EAGAIN)
            ret = 0;

        if (ret < 0) {
            error = errno;
            break;
        }

        offset += (std::size_t)ret;
        if (offset == size)
            break;

        if (ret == 0) {
            wait_nsec *= 2;
            if (wait_nsec > WAIT_MAX)
                wait_nsec = WAIT_MAX;
        }
        else {
            wait_nsec /= 2;
            if (wait_nsec < WAIT_MIN)
                wait_nsec = WAIT_MIN;
        }

        co_await coke::sleep(wait_nsec);
    }

    co_return error;
}


coke::Task<> HttpFileServer::process(coke::HttpServerContext ctx) {
    coke::HttpRequest &req = ctx.get_req();
    std::string method;

    req.get_method(method);

    for (char &c : method)
        c = std::tolower(c);

    try {
        if (method == "get") {
            co_await handle_get(ctx);
        }
        else if (method == "head") {
            co_await handle_head(ctx);
        }
        else {
            co_await handle_other(ctx, method);
        }
    }
    catch (const std::exception &e) {
        KLOG_ERROR("Exception method:{} what:{}", method, e.what());
    }
}

coke::Task<> HttpFileServer::handle_get(coke::HttpServerContext &ctx) {
    int64_t start = current_steady_usec();

    coke::HttpRequest &req = ctx.get_req();
    coke::HttpResponse &resp = ctx.get_resp();

    fs::path path = get_path(req);
    std::string relaitve = "." + path.string();
    fs::path full_path(params.root);
    full_path.append(relaitve);

    std::string file_path = full_path.string();
    int fd = open(file_path.c_str(), O_RDONLY);
    FdGuard fd_guard(fd);

    bool need_reply = false;
    off64_t filesize = -1;
    std::string body;
    int http_status = 0;
    int file_error = 0;

    if (fd >= 0)
        filesize = get_filesize(fd);

    // TODO 这里没实现path是目录的情况

    if (fd < 0 || filesize == off64_t(-1)) {
        http_status = 404;
        need_reply = true;
    }
    else if ((std::size_t)filesize <= params.min_size_hint) {
        // 小文件直接使用task response返回

        body.resize(filesize);
        auto res = co_await coke::pread(fd, body.data(), (std::size_t)filesize, 0);

        if (res.state == coke::STATE_SUCCESS) {
            http_status = 200;
            resp.set_header_pair("Content-Disposition", path.filename().string());
            resp.append_output_body_nocopy(body.data(), body.size());
            // 只要body的生命周期长于ctx.reply()就可以使用nocopy的接口了
        }
        else {
            http_status = 500;
            file_error = res.error;
        }

        need_reply = true;
    }
    else {
        // 大文件使用伪异步模式返回，由于涉及异步，这里的buf不能使用thread local模式的buf，
        // 只好每个任务分配一个
        char *buf = (char *)std::aligned_alloc(8192, RBUF_SIZE);
        std::unique_ptr<char, void (*)(void *)> buf_guard(buf, std::free);

        std::string header = get_http_header(filesize, path.filename().string());
        auto *task = ctx.get_task();
        int reply_error = 0;
        off64_t offset = 0;
        int64_t cost = current_steady_usec() - start;

        // task->noreply()通知workflow我们已经接管了该任务的回复操作
        task->noreply();
        reply_error = co_await reply_buf(task, header.data(), header.size());
        while (reply_error == 0) {
            auto res = co_await coke::pread(fd, buf, RBUF_SIZE, offset);
            if (res.state != coke::STATE_SUCCESS) {
                file_error = res.error;
                break;
            }
            else if (res.nbytes == 0)
                break;

            reply_error = co_await reply_buf(task, buf, (std::size_t)res.nbytes);
            offset += res.nbytes;

            KLOG_INFO("PartialReply path:{} total:{} offset:{} reply_error:{}",
                      path.string(), filesize, offset, reply_error);

            /// what if offset > filesize
        }

        int64_t reply_cost = current_steady_usec() - start - cost;
        if (file_error != 0 || reply_error != 0) {
            task->set_keep_alive(0);
            http_status = 500;
        }
        else {
            http_status = 200;
        }

        KLOG_INFO("method:{} path:{} status:{} file_error:{} reply_error:{} cost:{} reply:{}",
                  "get", path.string(), http_status, file_error, reply_error, cost, reply_cost);
    }

    if (need_reply) {
        protocol::HttpUtil::set_response_status(&resp, http_status);
        int64_t cost = current_steady_usec() - start;
        auto res = co_await ctx.reply();
        int64_t reply_cost = current_steady_usec() - start - cost;

        KLOG_INFO("method:{} path:{} status:{} file_error:{} reply_error:{} cost:{} reply:{}",
                  "get", path.string(), http_status, file_error, res.error, cost, reply_cost);
    }

    co_return;
}

coke::Task<> HttpFileServer::handle_head(coke::HttpServerContext &ctx) {
    int64_t start = current_steady_usec();

    fs::path path = get_path(ctx.get_req());
    coke::HttpResponse &resp = ctx.get_resp();
    int http_status = 501;

    // TODO head请求暂未实现

    protocol::HttpUtil::set_response_status(&resp, http_status);

    int64_t cost = current_steady_usec() - start;
    auto res = co_await ctx.reply();
    int64_t reply_cost = current_steady_usec() - start - cost;

    KLOG_INFO("method:{} path:{} status:{} file_error:{} reply_error:{} cost:{} reply:{}",
              "head", path.string(), http_status, 0, res.error, cost, reply_cost);
    co_return;
}

coke::Task<> HttpFileServer::handle_other(coke::HttpServerContext &ctx,
                                          const std::string &method) {
    int64_t start = current_steady_usec();
    coke::HttpResponse &resp = ctx.get_resp();
    fs::path path = get_path(ctx.get_req());

    // 本服务是只读服务，暂未支持其他请求方法
    protocol::HttpUtil::set_response_status(&resp, 405);
    int64_t cost = current_steady_usec() - start;
    auto res = co_await ctx.reply();
    int64_t reply_cost = current_steady_usec() - start - cost;

    KLOG_INFO("method:{} path:{} status:{} reply_error:{} cost:{} reply:{}",
              method, path.string(), 405, res.error, cost, reply_cost);

    co_return;
}
