#ifndef FILE_SERVER_OSTREAM_LOGGER_H
#define FILE_SERVER_OSTREAM_LOGGER_H

#include <ostream>
#include <atomic>

#include "log/log.h"

class OstreamLogger : public Logger {
public:
    OstreamLogger(std::ostream *os = nullptr) : os(os) { }

    void set_flush_level(int level) {
        flush_level = level;
    }

    void reset_ostream(std::ostream *os) {
        this->os.store(os, std::memory_order_relaxed);
    }

protected:
    virtual void do_log(int level, std::string_view sv) override {
        std::ostream *o = os.load(std::memory_order_relaxed);

        if (o) {
            (*o) << sv;

            if (level >= flush_level)
                o->flush();
        }
    }

private:
    std::atomic<std::ostream *> os;

    int flush_level{Logger::error};
};

#endif // FILE_SERVER_OSTREAM_LOGGER_H
