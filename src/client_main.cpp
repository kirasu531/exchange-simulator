#include "network_utils.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <cassert>
#include <charconv>
#include <string>

constexpr size_t MAX_LINE_SIZE = 256;

bool ParseInt(const std::string& s, int& value) {
    if ( s.empty() ) return false;

    auto [ptr, ec] = std::from_chars(
        s.data(),
        s.data() + s.size(),
        value
    );

    return ec == std::errc{} && ptr == s.data() + s.size();
}

int main(int argc, char *argv[]){
    int server_port = -1;

    for ( int i = 1; i < argc;){
        std::string arg = argv[i];

        if ( arg == "--port" ){
            if ( i + 1 == argc ){
                std::cerr << "--port requires a port\n";
                return 1;
            }

            if ( !ParseInt(argv[i + 1], server_port) || !(1 <= server_port && server_port <= 65535) ){
                std::cerr << "Server port must be an integer from 1 to 65535\n";
                return 1;
            }

            i += 2;
        } else{
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }

    if ( server_port == -1 ){
        std::cout << "Default port [4000] chosen\n";
        server_port = 4000; 
    }

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if ( client_fd < 0 ){
        perror("socket");
        return 0;
    }

    sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_port = htons(server_port);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");

    int connect_verdict = connect(
        client_fd, 
        reinterpret_cast <const sockaddr*> (&address), 
        sizeof(address)
    );

    if ( connect_verdict == -1 ){
        perror("connect");
        close(client_fd);

        return 0;
    }

    std::cout << "Connection successful\n";

    std::string pending, line;
    char data[MAX_LINE_SIZE];

    std::string command;

    while (std::getline(std::cin, command)){
        command += '\n';

        IOResult send_result = SendAll(client_fd, command.data(), command.size());

        if ( send_result == IOResult::Error ){
            perror("send");
            return 1;
        }

        RecvLineResult recv_result = RecvLine(client_fd, pending, line, data, sizeof(data));
        
        if ( recv_result == RecvLineResult::IOError ){
            perror("recvline");
            return 1;
        }  

        if ( recv_result == RecvLineResult::Closed ){
            std::cout << "Server closed\n";
            break;
        }

        if ( recv_result == RecvLineResult::TooLarge ){
            std::cerr << "Receive result is too large\n";
            return 1;
        }

        std::cout << line << '\n';
    }

    close(client_fd);
}