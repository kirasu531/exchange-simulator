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
#include <functional>
#include <mutex>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <charconv>
#include <atomic>

enum class CommandType{
    Add,
    Cancel,
    Get,
    Trades,
    Invalid
};

constexpr size_t MAX_LINE_SIZE = 256;

std::string log_path;
bool persistence_enabled, log_dirty = false;

int log_fd;

std::mutex log_mutex;

std::atomic <bool> keep_syncing;

std::vector <std::string> divide(const std::string &s){
    std::vector <std::string> str;

    for ( int i = 0; i < s.size(); i++ ){
        if ( s[i] == ' ' ) continue;

        str.push_back("");

        int j = i;

        while ( j < s.size() && s[j] != ' ' ){
            str.back().push_back(s[j]);
            j += 1;
        }

        std::swap(i, j);
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

void RunLogSyncLoop(int fd, int T){
    auto next_sync_time = std::chrono::steady_clock().now();

    while ( keep_syncing ){
        next_sync_time += std::chrono::milliseconds(T);
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

CommandType CheckCommand(const std::string &msg, Order &order){
    std::vector <std::string> vec = divide(msg);

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

void HandleClient(int client_fd, MatchingEngine &engine, std::mutex &engine_mutex){
    char buffer[MAX_LINE_SIZE];

    std::string pending, line;

    while ( true ){
        auto receive_result = RecvLine(
            client_fd,
            pending,
            line,
            buffer,
            sizeof(buffer)
        );

        if ( receive_result == RecvLineResult::Closed ){
            std::cout << "Client disconnected\n";
            break;
        }

        if ( receive_result == RecvLineResult::IOError ){
            perror("receiving from client");
            break;
        }

        if ( receive_result == RecvLineResult::TooLarge ){
            std::cout << "Command size is too large\n";
            break;
        }

        Order order;
        CommandType type = CheckCommand(line, order);

        std::string verdict;
        bool closeConnection = false;

        if ( type == CommandType::Invalid ){
            verdict = "Invalid Query";
        } else{
            {
                std::lock_guard <std::mutex> lock(engine_mutex);
                verdict = HandleCommand(engine, order, type);
            
                if ( verdict == "Cancelled" || verdict == "Accepted" ){
                    std::lock_guard <std::mutex> lock(log_mutex);
                    
                    if ( !AppendLogRecord(log_fd, line) ){
                        verdict = "Persistence Error";
                        closeConnection = true;
                    }
                }
            }
        }
        
        verdict += '\n';
        line.clear();
        
        if ( SendAll(client_fd, verdict.data(), verdict.size()) == IOResult::Error ){
            perror("sending to client");
            break;
        }

        if ( closeConnection ) break;
    }
        
    close(client_fd);
}

void RestoreLog(const std::string &filename, MatchingEngine &engine){
    if ( !persistence_enabled ) return; 

    std::ifstream file(filename);

    std::string line;

    while ( std::getline(file, line) ){
        if ( file.eof() ) continue;

        std::istringstream input(line);
        
        std::string type, extra;

        if ( !(input >> type) ) continue;

        if ( type == "ADD" ){
            Order order;
            std::string side, type;

            if ( !(input 
                >> order.orderId 
                >> order.price 
                >> order.quantity 
                >> side 
                >> type
                >> order.instrument) 
            ){
                continue;
            }

            if ( side == "BUY" ) order.side = Side::Buy;
            else if ( side == "SELL" ) order.side = Side::Sell;
            else continue;

            if ( type == "LIMIT" ) order.type = OrderType::Limit;
            else if ( type == "MARKET" ) order.type = OrderType::Market;
            else continue;

            if ( input >> extra ) continue;
            
            engine.AddOrder(order);
        } else if ( type == "CANCEL" ){
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

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if ( server_fd == -1 ){
        perror("socket");
        return 1;
    }

    int opt = 1;
    int setSocket_result = setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    if ( setSocket_result == -1 ){
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

    int listen_verdict = listen(server_fd, 3);

    if ( listen_verdict != 0 ){
        perror("listen");
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

    while ( true ){
        std::cout << "Waiting for client...\n";

        sockaddr_in client_address;
        socklen_t client_address_size = sizeof(client_address);

        int client_fd = accept(
            server_fd, 
            reinterpret_cast <sockaddr*> (&client_address), 
            &client_address_size
        );

        if ( client_fd == -1 ){
            perror("accept");
            continue;
        }

        std::cout << "Client connected\n";

        std::thread(
            HandleClient,
            client_fd,
            std::ref(engine),
            std::ref(engine_mutex)
        ).detach();
    }

    keep_syncing = false;
    sync_thread.join();

    close(server_fd);

    if ( persistence_enabled ) close(log_fd);
}