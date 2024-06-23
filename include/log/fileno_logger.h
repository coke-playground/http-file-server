#ifndef FILE_SERVER_FILENO_LOGGER_H
#define FILE_SERVER_FILENO_LOGGER_H

#include <unistd.h>

class FilenoLogger : public Logger {
public:
    FilenoLogger(int fd) : fd(fd) { }

protected:
    virtual void do_log(int, std::string_view sv) override {
        // TODO write调用可能仅写入了部分数据，此处忽略了这种错误
        (void)write(fd, sv.data(), sv.size());
    }

private:
    int fd;
};

#endif // FILE_SERVER_FILENO_LOGGER_H
