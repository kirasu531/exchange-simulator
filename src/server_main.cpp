#include "matching_engine.hpp"
#include "network_utils.hpp"
#include "protocol.hpp"
#include "persistent_log.hpp"
#include "client_manager.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <sys/epoll.h>
#include <unordered_map>
#include <deque>
#include <iterator>
#include <algorithm>
#include <cstdint>

struct LogEntry {
    uint64_t index;
    Order order;
    CommandType type;
};

// epoll
int epfd;

class ReplicatedLog {
private:
    uint64_t lastLogIndex = 0;
    uint64_t commitIndex = 0;

    std::deque <LogEntry> record;

    std::unordered_map <uint64_t, LogEntry> log;

public:
    void Append(const Order &order, const CommandType &type){
        lastLogIndex += 1;

        log[lastLogIndex] = LogEntry{
            lastLogIndex,
            order,
            type
        };

        record.push_back(log[lastLogIndex]);
    }

    bool Remove(uint64_t index){ // clears index from log
        auto it = log.find(index);

        if ( it == log.end() ) return false;

        log.erase(it);
        return true;
    }

    bool CommitThrough(uint64_t index){
        if ( index <= commitIndex || index > lastLogIndex ) return false;

        commitIndex = index;
        return true;
    }

    LogEntry GetEntry(uint64_t index){
        return log[index]; // lazy handling for now
    }

    uint64_t GetCommitIndex(){
        return commitIndex;
    }

    uint64_t GetLastLogIndex(){
        return lastLogIndex;
    }
};

int main(int argc, char *argv[]){
    int server_port = -1;
    int sync_interval_ms = -1;

    std::string log_path;

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
        } else if ( arg == "--log" ){
            if ( i + 1 == argc ){
                std::cerr << "--log requires a path\n";
                return 1;
            }

            std::string value = argv[i + 1];

            if ( value.starts_with("--") ){
                std::cerr << "--log requires a path\n";
                return 1;
            }

            log_path = argv[i + 1];
            i += 2;
        } else if ( arg == "--sync-interval-ms" ){
            if ( i + 1 == argc ){
                std::cerr << "--sync-interval-ms requires a time (ms)\n";
                return 1;
            }

            if ( !ParseInt(argv[i + 1], sync_interval_ms) || sync_interval_ms <= 0 ){
                std::cerr << "Sync interval time must be a positive integer\n";
                return 1;
            }

            i += 2;
        } else{
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }

    if ( sync_interval_ms > 0 && log_path.empty() ){
        std::cerr << "--sync-interval-ms requires --log\n";
        return 1;
    }

    if ( server_port == -1 ){
        std::cout << "Default port [4000] chosen\n";
        server_port = 4000;
    }

    PersistentLog persistent_log(log_path, sync_interval_ms);

    if ( !persistent_log.Open() ) return 1;

    int server_fd = socket(
        AF_INET,
        SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0
    );

    if ( server_fd == -1 ){
        perror("socket");
        return 1;
    }

    int opt = 1;
    int setsockopt_result = setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    if ( setsockopt_result == -1 ){
        perror("setsockopt");
        return 1;
    }

    sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_port = htons(server_port);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");

    int bind_result = bind(
        server_fd,
        reinterpret_cast <const sockaddr*> (&address),
        sizeof(address)
    );

    if ( bind_result != 0 ){
        perror("bind");
        return 1;
    }

    int listen_result = listen(server_fd, SOMAXCONN);

    if ( listen_result != 0 ){
        perror("listen");
        return 1;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);

    if ( epfd == -1 ){
        perror("epoll_create1");
        return 1;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_fd;

    if ( epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &event) == -1 ){
        perror("epoll_ctl");
        return 1;
    }

    int flags = fcntl(server_fd, F_GETFL);

    if ( flags == -1 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1 ){
        perror("fcntl");
        return 1;
    }

    MatchingEngine engine;

    persistent_log.Replay(engine);
    persistent_log.StartSync();

    ClientManager client_manager(epfd);

    epoll_event events[1024];

    char read_buffer[READ_BUDGET];

    while ( true ){
        int ready_events = epoll_wait(epfd, events, std::size(events), -1);

        if ( ready_events == -1 ){
            if ( errno == EINTR ) continue;

            perror("epoll_wait");
            break;
        }

        for ( int i = 0; i < ready_events; i++ ){
            int fd = events[i].data.fd;
            uint32_t event_flags = events[i].events;

            if ( fd == server_fd ){ // listen
                while ( true ){
                    int client_fd = accept4(
                        server_fd,
                        nullptr,
                        nullptr,
                        SOCK_NONBLOCK | SOCK_CLOEXEC
                    );

                    if ( client_fd == -1 ){
                        if ( errno == EINTR ) continue;

                        if ( errno != EAGAIN && errno != EWOULDBLOCK ){
                            perror("accept4");
                        }

                        break;
                    }

                    epoll_event ev{};
                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;

                    if ( epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) == -1 ){
                        perror("epoll_ctl add");
                        close(client_fd);
                        continue;
                    }

                    std::cout << "Client connected\n";

                    client_manager.Register(client_fd);
                }
            } else{
                auto client_ptr = client_manager.Find(fd);

                if ( client_ptr == nullptr ) continue;

                auto &client = *client_ptr;
                bool alive = true;

                if ( event_flags & EPOLLERR ) alive = false;

                if ( alive && (event_flags & (EPOLLIN | EPOLLHUP)) ){
                    auto result = Recv(
                        fd,
                        client.input_buffer,
                        read_buffer,
                        sizeof(read_buffer)
                    );

                    if ( result == IOResult::Closed ){
                        alive = false;
                    }
                    else if ( result == IOResult::Error ){
                        perror("Recv");
                        alive = false;
                    }
                    else{
                        std::string &input = client.input_buffer;
                        std::string line;

                        for ( size_t j = 0; j < input.size() && alive; j++ ){
                            if ( input[j] == '\n' ){
                                while ( !line.empty() && line.back() == '\r' ){
                                    line.pop_back();
                                }

                                auto cmd = ParseCommand(line);

                                std::string reply;
                                std::vector <Trade> new_trades;

                                if ( cmd.type == CommandType::Invalid ){
                                    reply = "Invalid Query";
                                }
                                else if ( cmd.type == CommandType::Subscribe ){
                                    if ( client_manager.Subscribe(fd, client, cmd.order.instrument) ){
                                        reply = "Subscribed";
                                    } else{
                                        reply = "Already subscribed";
                                    }
                                }
                                else if ( cmd.type == CommandType::Unsubscribe ){
                                    if ( client_manager.Unsubscribe(fd, client, cmd.order.instrument) ){
                                        reply = "Unsubscribed";
                                    } else{
                                        reply = "Not Subscribed";
                                    }
                                }
                                else{
                                    CommandResult outcome;

                                    outcome = HandleCommand(engine, cmd.order, cmd.type);
                                    reply = outcome.reply;

                                    if ( outcome.mutates && !persistent_log.Append(line) ){
                                        reply = "Persistence Error";
                                        alive = false;
                                    }

                                    new_trades = std::move(outcome.new_trades);
                                }

                                client.output_buffer += reply + '\n';
                                line.clear();

                                if ( !new_trades.empty() ){
                                    client_manager.PublishTrades(
                                        cmd.order.instrument,
                                        new_trades
                                    );
                                }

                                if ( client.pendingBytes() > PENDING_OUTPUT_LIMIT ){
                                    alive = false;
                                }
                            } else{
                                line += input[j];

                                if ( line.size() > MAX_LINE_SIZE ) alive = false;
                            }
                        }

                        std::swap(line, input);
                    }
                }

                if ( alive && (event_flags & EPOLLOUT) ){
                    size_t sent = 0;

                    IOResult result = Send(
                        fd,
                        client.getSendData(),
                        std::min(SEND_BUDGET, client.pendingBytes()),
                        sent
                    );

                    if ( result == IOResult::Closed ){
                        alive = false;
                    }
                    else if ( result == IOResult::Error ){
                        perror("Send");
                        alive = false;
                    }
                    else{
                        client.sent_offset += sent;
                    }
                }

                if ( alive == true ){
                    client_manager.QueueEpollUpdate(fd);
                } else{
                    client_manager.QueueDisconnect(fd);
                }
            }
        }

        client_manager.FlushEpollUpdates();
        client_manager.FlushDisconnects();
    }

    close(server_fd);
}
