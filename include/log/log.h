#ifndef FILE_SERVER_LOG_H
#define FILE_SERVER_LOG_H

#include <chrono>
#include <ctime>
#include <format>
#include <iostream>
#include <iterator>
#include <memory_resource>
#include <source_location>
#include <string>
#include <string_view>

#include <sys/time.h>

#ifndef LOGGER_PATH_DELIMITER
#define LOGGER_PATH_DELIMITER '/'
#endif

class Logger {
    static char *get_thread_local_buf(std::size_t &size);
    static const char *get_level_str(int level);

    static constexpr const char *do_split_filename(const char *p) {
        const char *q = p;
        while (*p) {
            if (*p == LOGGER_PATH_DELIMITER)
                q = p + 1;
            ++p;
        }
        return q;
    }

public:
    constexpr static int trace      = 0;
    constexpr static int debug      = 1;
    constexpr static int info       = 2;
    constexpr static int warn       = 3;
    constexpr static int error      = 4;
    constexpr static int num_level  = 5;

    static Logger *get_default_logger();
    static void set_default_logger(Logger *logger);

public:
    void set_log_level(int level) {
        log_level = level;
    }

    void set_show_time(bool show_time) {
        this->show_time = show_time;
    }

    void set_show_log_pos(bool show_log_pos) {
        this->show_log_pos = show_log_pos;
    }

    void set_split_filename(bool split_filename) {
        this->split_filename = split_filename;
    }

    void set_line_end(const std::string &line_end) {
        this->line_end = line_end;
    }

    template<typename... Args>
    void log(const std::source_location &sl, int level,
             std::format_string<Args...> str, Args&&... args) {
        if (level < log_level)
            return;

        std::size_t buf_size;
        char *local_buf = get_thread_local_buf(buf_size);
        std::pmr::monotonic_buffer_resource res(local_buf, buf_size);
        std::pmr::string buf(&res);

        // Avoid the impact of address alignment
        if (buf_size > 64)
            buf.reserve(buf_size - 64);

        if (show_time)
            format_time(buf);

        if (show_level)
            buf.append(get_level_str(level)).append(" ");

        if (show_log_pos)
            format_location(buf, sl);

        std::format_to(std::back_inserter(buf), str, std::forward<Args>(args)...);
        buf.append(line_end);

        do_log(level, buf);
    }

protected:
    virtual void do_log(int level, std::string_view sv) = 0;

    void format_time(std::pmr::string &buf) {
        static auto current_zone = std::chrono::current_zone();
        auto now = std::chrono::system_clock::now();
        std::chrono::zoned_time ztime(current_zone, now);
        std::format_to(std::back_inserter(buf),
                       "[{:%Y-%m-%d %H:%M:%S}] ", ztime);
    }

    void format_location(std::pmr::string &buf, const std::source_location &l) {
        const char *filename;

        if (split_filename)
            filename = do_split_filename(l.file_name());
        else
            filename = l.file_name();

        std::format_to(std::back_inserter(buf), "{}:{} ", filename, l.line());
    }

protected:
    bool show_time{true};
    bool show_level{true};
    bool show_log_pos{true};
    bool split_filename{true};

    int log_level{2};
    std::string line_end{"\n"};
};

inline const char *Logger::get_level_str(int level) {
    switch (level) {
    case Logger::trace: return "TRACE";
    case Logger::debug: return "DEBUG";
    case Logger::info:  return "INFO";
    case Logger::warn:  return "WARN";
    case Logger::error: return "ERROR";
    default:            return "UNKNOWN";
    }
}

#define LOGGER_DO_LOG(instance, level, format, ...)                 \
do {                                                                \
    instance->log(std::source_location::current(), level, format    \
                  __VA_OPT__(,) __VA_ARGS__);                       \
} while(0)

#define LOGGER_DEFAULT(level, format, ...) \
    LOGGER_DO_LOG(Logger::get_default_logger(), level, format, ##__VA_ARGS__)

#define LOG_TRACE(format, ...) LOGGER_DEFAULT(Logger::trace, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOGGER_DEFAULT(Logger::debug, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  LOGGER_DEFAULT(Logger::info,  format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  LOGGER_DEFAULT(Logger::warn,  format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOGGER_DEFAULT(Logger::error, format, ##__VA_ARGS__)

#endif // FILE_SERVER_LOG_H
