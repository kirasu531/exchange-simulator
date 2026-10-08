#include "network_utils.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <iostream>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cerrno>

std::string logPath;

int file_fd, operations, batch;

std::vector <std::string> commands;

std::chrono::time_point<std::chrono::steady_clock> start;

void ResetTime(auto &start){
    start = std::chrono::steady_clock::now();
}

double GetRuntimeSec(const auto &start){
    auto duration = std::chrono::steady_clock::now() - start;

    return std::chrono::duration <double> (duration).count();
}

bool ParseInt(const std::string& s, int& value) {
    if ( s.empty() ) return false;

    auto [ptr, ec] = std::from_chars(
        s.data(),
        s.data() + s.size(),
        value
    );

    return ec == std::errc{} && ptr == s.data() + s.size();
}

bool AppendLog(int fd, const std::string &line){
    std::string record = line + '\n';

    if ( WriteAll(fd, record.data(), record.size()) == IOResult::Error ){
        perror("write log");
        return false;
    }

    return true;
}

bool LogSync(int fd){
    while ( fdatasync(fd) == -1 ){
        if ( errno == EINTR ) continue;

        perror("sync");
        return false;
    }

    return true;
}

int main(int argc, char *argv[]){
    if ( argc != 4 || !ParseInt(argv[2], operations) || !ParseInt(argv[3], batch) ){
        std::cout << "Usage: " << argv[0] << " <logpath> <operations> <batchsize>\n";
        return 1;
    }

    if ( operations <= 0 || batch <= 0 ){
        std::cout << "Operations and batch size must be positive\n";
        return 1;
    }

    logPath = argv[1];

    file_fd = open(
        logPath.c_str(),
        O_WRONLY | O_CREAT | O_APPEND,
        0644
    );

    if ( file_fd == -1 ){
        perror("file open");
        return 1;
    }

    commands.resize(operations);
    for ( int i = 0; i < operations; i++ ){
        commands[i] = "ADD 1234 123456 123456 SELL LIMIT BANANAS";
    }

    ResetTime(start);

    for ( int i = 0; i < operations; i += batch ){
        int nxt = std::min(i + batch, operations);

        for ( int j = i; j < nxt; j++ ){
            if ( !AppendLog(file_fd, commands[j]) ){
                return 1;
            }
        }

        if ( !LogSync(file_fd) ) return 1;
    }

    double time = GetRuntimeSec(start);

    std::cout << std::fixed << std::setprecision(6);

    std::cout << operations << ' ' << batch << ' ' << time << '\n';

    close(file_fd);
}