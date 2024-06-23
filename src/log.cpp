#include <iostream>
#include <memory>

#include "log/log.h"
#include "log/ostream_logger.h"

static Logger *cout_logger_instance() {
    static OstreamLogger logger(&std::cout);
    return &logger;
}

static Logger *default_logger(Logger *new_logger) {
    // TODO 也可以实现一个可以随时更换logger的版本，但在大部分情况下没有这种需求
    static Logger *logger = cout_logger_instance();

    if (new_logger)
        logger = new_logger;

    return logger;
}

char *Logger::get_thread_local_buf(std::size_t &size) {
    constexpr static int LOG_BUF_SIZE = 16 * 1024;
    static thread_local std::unique_ptr<char []> buf(new char[LOG_BUF_SIZE]);

    size = LOG_BUF_SIZE;
    return buf.get();
}

Logger *Logger::get_default_logger() {
    return default_logger(nullptr);
}

void Logger::set_default_logger(Logger *logger) {
    default_logger(logger);
}
