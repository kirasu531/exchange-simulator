#pragma once

#include <string>

enum class IOResult {
    Success,
    Closed,
    Error
};

enum class RecvLineResult {
    Success,
    Closed,
    IOError,
    TooLarge
};

RecvLineResult RecvLine(int fd, std::string& pending, std::string& line, void* data, size_t size);

IOResult RecvAll(int fd, void* data, size_t size);
IOResult SendAll(int fd, const void *data, size_t size);

IOResult WriteAll(int fd, const void* data, size_t size);