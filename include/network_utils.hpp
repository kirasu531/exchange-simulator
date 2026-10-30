#pragma once

#include <string>

enum class IOResult {
    Success,
    WouldBlock,
    Closed,
    Error
};

enum class RecvLineResult {
    Success,
    Closed,
    IOError,
    TooLarge
};

constexpr size_t MAX_LINE_SIZE = 256;

constexpr size_t READ_BUDGET = 4096;
constexpr size_t SEND_BUDGET = 4096; 
constexpr size_t PENDING_OUTPUT_LIMIT = 64 * 1024;

RecvLineResult RecvLine(int fd, std::string& pending, std::string& line, void* data, size_t size);

IOResult SendAll(int fd, const void *data, size_t size);

IOResult Recv(int fd, std::string &pending, void *data, size_t size);
IOResult Send(int fd, const void *data, size_t size, size_t &sent);

IOResult WriteAll(int fd, const void* data, size_t size);

struct Connection{
    std::string input_buffer;
    std::string output_buffer;
    size_t sent_offset = 0;
    
    bool watching_output = false;

    bool hasPendingData() const{
        return sent_offset < output_buffer.size();
    }

    const void* getSendData() const{
        if ( sent_offset >= output_buffer.size() ){
            return nullptr;
        }

        return output_buffer.data() + sent_offset;
    }

    size_t pendingBytes() const{
        return output_buffer.size() - sent_offset;
    }
};