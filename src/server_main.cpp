#include "matching_engine.hpp"
#include "network_utils.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <charconv>
#include <atomic>
#include <sys/epoll.h>
#include <unordered_map>
#include <iterator>

enum class CommandType{
    Add,
    Cancel,
    Get,
    Trades,
    Invalid
};

std::string log_path;
bool persistence_enabled, log_dirty = false;

int log_fd, epfd;

std::mutex log_mutex;

std::atomic <bool> keep_syncing;

std::unordered_map <int, Connection> connections;

std::vector <std::string> tokenize(const std::string &s){
    std::vector <std::string> str;

    for ( int i = 0; i < s.size(); i++ ){
        if ( s[i] == ' ' ) continue;

        str.push_back("");

        int j = i;

        while ( j < s.size() && s[j] != ' ' ){
            str.back().push_back(s[j]);
            j += 1;
        }

        i = j;
    }

    return str;
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

bool AppendLogRecord(int fd, const std::string &line){
    if ( !persistence_enabled ) return true;

    std::string record = line + '\n';

    if ( WriteAll(fd, record.data(), record.size()) == IOResult::Error ){
        perror("write log");
        return false;
    }

    log_dirty = true;
    return true;
}

void RunLogSyncLoop(int fd, int interval_ms){
    auto next_sync_time = std::chrono::steady_clock().now();

    while ( keep_syncing ){
        next_sync_time += std::chrono::milliseconds(interval_ms);
        std::this_thread::sleep_until(next_sync_time);

        std::lock_guard <std::mutex> lock(log_mutex);

        if ( !log_dirty ) continue;

        while ( fdatasync(fd) == -1 ){
            if ( errno == EINTR ) continue;

            perror("sync");
            return;
        }

        log_dirty = false;
    }
}

CommandType ParseCommand(const std::string &msg, Order &order){
    std::vector <std::string> vec = tokenize(msg);

    if ( vec.empty() ) return CommandType::Invalid;

    if ( vec[0] == "ADD" ){
        if ( vec.size() != 7 ) return CommandType::Invalid;
        
        if (
            !ParseInt(vec[1], order.orderId) ||
            !ParseInt(vec[2], order.price) || 
            !ParseInt(vec[3], order.quantity) || 
            (vec[4] != "BUY" && vec[4] != "SELL") || 
            (vec[5] != "LIMIT" && vec[5] != "MARKET")
        ){
            
            return CommandType::Invalid;
        }

        
        order.side = vec[4] == "BUY" ? Side::Buy : Side::Sell;
        order.type = vec[5] == "LIMIT" ? OrderType::Limit : OrderType::Market;
        order.instrument = vec[6];
        
        if ( !IsValidOrder(order) ) return CommandType::Invalid;

        return CommandType::Add;
    }

    if ( vec[0] == "CANCEL" ){
        if ( vec.size() != 2 || !ParseInt(vec[1], order.orderId) || order.orderId < 0 ){
            return CommandType::Invalid;
        }
        
        return CommandType::Cancel;
    }

    if ( vec[0] == "GET" ){
        if ( vec.size() != 2 || !ParseInt(vec[1], order.orderId) || order.orderId < 0 ){
            return CommandType::Invalid;
        }

        return CommandType::Get;
    }

    if ( vec[0] == "TRADES" ){
        if ( vec.size() != 1 ) return CommandType::Invalid;

        return CommandType::Trades;
    }

    return CommandType::Invalid;
}

std::string HandleCommand(MatchingEngine &engine, const Order &order, const CommandType &type){
    if ( type == CommandType::Add ){
        auto verdict = engine.AddOrder(order);

        if ( verdict == AddOrderResult::Accepted ) return "Accepted";
        if ( verdict == AddOrderResult::DuplicateId ) return "Duplicate Id";

        return "Invalid Query";
    }

    if ( type == CommandType::Cancel ){
        auto verdict = engine.CancelOrder(order.orderId);

        if ( verdict == CancelResult::Cancelled ) return "Cancelled";
        
        return "Not Found";
    }

    if ( type == CommandType::Get ){
        auto result = engine.GetOrder(order.orderId);

        if ( result == std::nullopt ) return "Not Found";

        return std::string(
            std::to_string(result -> orderId) + " " + 
            std::to_string(result -> price) + " " + 
            std::to_string(result -> quantity) + " " +  
            (result -> side == Side::Buy ? "Buy" : "Sell") + " " + 
            (result -> type == OrderType::Limit ? "Limit" : "Market") + " " + 
            result -> instrument
        );
    }

    if ( type == CommandType::Trades ){
        return std::to_string(engine.GetTrades().size());
    }

    return "Invalid Query";
}

void RestoreLog(const std::string &filename, MatchingEngine &engine){
    if ( !persistence_enabled ) return; 

    std::ifstream file(filename);

    std::string line;

    while ( std::getline(file, line) ){
        if ( file.eof() ) continue;

        std::istringstream input(line);

        std::string command, extra;

        if ( !(input >> command) ) continue;

        if ( command == "ADD" ){
            Order order;
            std::string side, order_type;

            if ( !(input
                >> order.orderId
                >> order.price
                >> order.quantity
                >> side
                >> order_type
                >> order.instrument)
            ){
                continue;
            }

            if ( side == "BUY" ) order.side = Side::Buy;
            else if ( side == "SELL" ) order.side = Side::Sell;
            else continue;

            if ( order_type == "LIMIT" ) order.type = OrderType::Limit;
            else if ( order_type == "MARKET" ) order.type = OrderType::Market;
            else continue;

            if ( input >> extra ) continue;

            engine.AddOrder(order);
        } else if ( command == "CANCEL" ){
            int orderId; 
            
            if ( !(input >> orderId) ) continue;
            if ( input >> extra ) continue;

            engine.CancelOrder(orderId);
        }
    }

    std::cout << "Restore successfull\n";
}

int main(int argc, char *argv[]){
    int server_port = -1;
    int sync_interval_ms = -1;

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

    persistence_enabled = !log_path.empty();

    if ( sync_interval_ms == -1 ){
        keep_syncing = false;
    } else{
        keep_syncing = true;
    }

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

    int bind_verdict = bind(
        server_fd, 
        reinterpret_cast <const sockaddr*> (&address), 
        sizeof(address)
    );

    if ( bind_verdict != 0 ){
        perror("bind");
        return 1;
    }

    int listen_verdict = listen(server_fd, SOMAXCONN);

    if ( listen_verdict != 0 ){
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

    if ( persistence_enabled ){
        log_fd = open(
            log_path.c_str(),
            O_WRONLY | O_CREAT | O_APPEND,
            0644
        );

        if ( log_fd == -1 ){
            perror("file open");
            return 1;
        }
    }

    std::thread sync_thread(
        RunLogSyncLoop,
        log_fd,
        sync_interval_ms
    );

    MatchingEngine engine;
    std::mutex engine_mutex;

    RestoreLog(log_path, engine);

    epoll_event events[1024];

    char data_read[READ_BUDGET];

    while ( true ){
        int count = epoll_wait(epfd, events, std::size(events), -1);

        if ( count == -1 ){
            if ( errno == EINTR ) continue;

            perror("epoll_wait");
            break;
        }

        for ( int i = 0; i < count; i++ ){
            int fd = events[i].data.fd;
            u_int32_t event_flags = events[i].events;

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

                    connections[client_fd] = Connection();
                }
            } else{
                auto it = connections.find(fd);

                if ( it == connections.end() ){
                    assert(false && "Unknown client fd");
                    continue;
                }
                
                bool alive = true;

                if ( event_flags & EPOLLERR ) alive = false;

                if ( alive && (event_flags & (EPOLLIN | EPOLLHUP)) ){
                    auto result = Recv(
                        fd, 
                        connections[fd].input_buffer,
                        data_read,
                        sizeof(data_read)
                    );

                    if ( result == IOResult::Closed ){
                        alive = false;
                    } 
                    else if ( result == IOResult::Error ){
                        perror("Recv");
                        alive = false;
                    } 
                    else{
                        std::string &input = connections[fd].input_buffer;
                        std::string pending;

                        for ( size_t i = 0; i < input.size() && alive; i++ ){
                            if ( input[i] == '\n' ){
                                while ( !pending.empty() && pending.back() == '\r' ){
                                    pending.pop_back();
                                }

                                Order order;
                                CommandType type = ParseCommand(pending, order);

                                std::string verdict;

                                if ( type == CommandType::Invalid ){
                                    verdict = "Invalid Query";
                                } else{
                                    {
                                        std::lock_guard <std::mutex> lock(engine_mutex);
                                        verdict = HandleCommand(engine, order, type);
                                    
                                        if ( verdict == "Cancelled" || verdict == "Accepted" ){
                                            std::lock_guard <std::mutex> lock(log_mutex);
                                            
                                            if ( !AppendLogRecord(log_fd, pending) ){
                                                verdict = "Persistence Error";
                                                alive = false;
                                            }
                                        }
                                    }
                                }
                                
                                connections[fd].output_buffer += verdict + '\n';
                                pending.clear();

                                if ( connections[fd].pendingBytes() > PENDING_OUTPUT_LIMIT ){
                                    alive = false;
                                }
                            } else{
                                pending += input[i];

                                if ( pending.size() > MAX_LINE_SIZE ) alive = false;
                            }
                        }

                        std::swap(pending, input);
                    }
                }

                if ( alive && (event_flags & EPOLLOUT) ){
                    auto &client = connections[fd];
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
                    bool has_pending_output = connections[fd].hasPendingData();

                    if ( has_pending_output != connections[fd].watching_output ){
                        epoll_event ev{};
                        ev.events = EPOLLIN;
                        ev.data.fd = fd;

                        if ( has_pending_output ) ev.events |= EPOLLOUT;

                        if ( epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1 ){
                            perror("epoll_ctl MOD");
                            alive = false;
                        }

                        connections[fd].watching_output = has_pending_output;
                    }

                    if ( !has_pending_output ){
                        connections[fd].output_buffer.clear();
                        connections[fd].sent_offset = 0;
                    }
                }

                if ( !alive ){
                    if ( epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) == -1 ){
                        perror("epoll_ctl DEL");
                    }

                    if ( close(fd) == -1 ) perror("close client");

                    std::cout << "Client disconnected/closed\n";
                    connections.erase(it);
                }
            }
        }
    }

    keep_syncing = false;
    sync_thread.join();

    close(server_fd);

    if ( persistence_enabled ) close(log_fd);
}