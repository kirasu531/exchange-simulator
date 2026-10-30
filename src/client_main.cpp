#include "network_utils.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <cassert>
#include <charconv>
#include <string>
#include <fcntl.h>
#include <sys/epoll.h>
#include <iterator>

char data_read[READ_BUDGET];

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

    int client_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if ( client_fd < 0 ){
        perror("socket");
        return 1;
    }

    int flags = fcntl(client_fd, F_GETFL);

    if ( flags == -1 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1 ){
        perror("fcntl");
        return 1;
    }

    sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_port = htons(server_port);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");

    while ( true ){
        int connect_verdict = connect(
            client_fd, 
            reinterpret_cast <const sockaddr*> (&address), 
            sizeof(address)
        );

        if ( connect_verdict == -1 ){
            if ( errno == EINPROGRESS ) continue;

            perror("connect");
            close(client_fd);
            return 1;
        } else{
            std::cout << "Connection successful\n";
            break;
        }
    }
    
    int epfd = epoll_create1(EPOLL_CLOEXEC);

    if ( epfd == -1 ){
        perror("epoll_create1");
        return 1;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = client_fd;

    if ( epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &event) == -1 ){
        perror("epoll_ctl");
        return 1;
    }

    event = epoll_event{};
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;

    if ( epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1 ){
        perror("epoll_ctl STDIN_FILENO");
        return 1;
    }

    epoll_event events[64];

    Connection client;

    while ( true ){
        int count = epoll_wait(epfd, events, std::size(events), -1);

        if ( count == -1 ){
            if ( errno == EINTR ) continue;

            perror("epoll_wait");
            return 1;
        }

        for ( int i = 0; i < count; i++ ){
            int fd = events[i].data.fd;
            u_int32_t event_flags = events[i].events;

            if ( fd == client_fd ){
                if ( event_flags & EPOLLIN ){
                    auto result = Recv(
                        client_fd,
                        client.input_buffer,
                        data_read,
                        sizeof(data_read)
                    );

                    if ( result == IOResult::Closed ){
                        std::cout << "Server closed\n";
                        return 0;
                    }
                    else if ( result == IOResult::Error ){
                        perror("Recv");
                        return 1;
                    }
                    else{
                        std::string &input = client.input_buffer;
                        std::string pending;

                        for ( size_t i = 0; i < input.size(); i++ ){
                            if ( input[i] == '\n' ){
                                std::cout << pending << '\n';
                                pending.clear();
                            } else{
                                pending.push_back(input[i]);
                            }
                        }

                        std::swap(pending, input);
                    }
                }

                if ( event_flags & EPOLLOUT ){
                    size_t sent = 0;
                    auto result = Send(
                        client_fd,
                        client.getSendData(),
                        std::min(SEND_BUDGET, client.pendingBytes()),
                        sent
                    );

                    if ( result == IOResult::Closed ){
                        std::cout << "Server closed\n";
                        return 0;
                    }
                    else if ( result == IOResult::Error ){
                        perror("Send");
                        return 1;
                    }
                    else{
                        client.sent_offset += sent;
                    }
                }
            } else{ // stdin
                std::string line;
                getline(std::cin, line);
                
                client.output_buffer += line + '\n';
            }

            bool has_pending_output = client.hasPendingData();

            if ( has_pending_output != client.watching_output ){
                epoll_event ev{};
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;

                if ( has_pending_output ) ev.events |= EPOLLOUT;

                if ( epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &ev) == -1 ){
                    perror("epoll_ctl MOD");
                    return 1;
                }

                client.watching_output = has_pending_output;
            }

            if ( !has_pending_output ){
                client.output_buffer.clear();
                client.sent_offset = 0;
            }
        }
    }

    close(client_fd);
}