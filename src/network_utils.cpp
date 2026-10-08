#include "network_utils.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <cassert>
#include <cerrno>

constexpr size_t MAX_LINE_SIZE = 256;

RecvLineResult RecvLine(int fd, std::string &pending, std::string &line, void *data, size_t size){
    line.clear();

    {
        bool found = false;
        int idx = -1;

        for ( int i = 0; size_t(i) < pending.size(); i++ ){
            if ( pending[i] == '\n' ){
                found = true;
                idx = i;
                
                break;
            }
        }

        if ( found ){
            std::string next;

            for ( int i = 0; size_t(i) < pending.size(); i++ ){
                if ( i < idx ) line.push_back(pending[i]);
                if ( i > idx ) next.push_back(pending[i]);
            }

            std::swap(pending, next);

            return RecvLineResult::Success;
        }
    }
    
    auto bytes = static_cast <char*> (data);

    while ( true ){
        ssize_t bytes_received = recv(fd, bytes, size, 0);

        if ( bytes_received ==  0 ) return RecvLineResult::Closed;
        if ( bytes_received == -1 ){
            if ( errno == EINTR ) continue;

            return RecvLineResult::IOError;
        }

        for ( int i = 0; i < bytes_received; i++ ){
            if ( *(bytes + i) == '\n' ){
                line += pending;
                pending.clear();

                for ( int j = i + 1; j < bytes_received; j++ ){
                    pending.push_back(*(bytes + j));
                }

                return RecvLineResult::Success;
            }

            pending.push_back(*(bytes + i));
        }

        if ( pending.size() > MAX_LINE_SIZE ){
            return RecvLineResult::TooLarge;
        }
    }

    assert(false); // function should never reach it
}

IOResult RecvAll(int fd, void *data, size_t size){
    auto bytes = static_cast <char*> (data);

    int received = 0;

    while ( received < size ){
        ssize_t result = recv(fd, bytes + received, size - received, 0);

        if ( result ==  0 ) return IOResult::Closed;
        if ( result == -1 ){
            if ( errno == EINTR ) continue;

            return IOResult::Error;
        }

        received += result;
    }

    return IOResult::Success;
}

IOResult SendAll(int fd, const void *data, size_t size){
    auto bytes = static_cast <const char*> (data);

    size_t sent = 0;

    while ( sent < size ){
        ssize_t result = send(
            fd,
            bytes + sent,
            size - sent,
            MSG_NOSIGNAL
        );

        if ( result ==  0 ) return IOResult::Error;
        if ( result == -1 ){
            if ( errno == EINTR ) continue;

            return IOResult::Error;
        }

        sent += result;
    }
    
    return IOResult::Success;
}

IOResult WriteAll(int fd, const void* data, size_t size){
    auto bytes = static_cast <const char*> (data);

    size_t written = 0;

    while ( written < size ){
        ssize_t result = write(
            fd,
            bytes + written,
            size - written
        );

        if ( result ==  0 ) return IOResult::Error;
        if ( result == -1 ){
            if ( errno == EINTR ) continue;
            
            return IOResult::Error;
        }

        written += result;
    }

    return IOResult::Success;
}