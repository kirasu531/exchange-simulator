#include "matching_engine.hpp"

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

std::string logPath;
bool persistenceEnabled;

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

bool isInteger(const std::string &s){
    if ( s.empty() ) return false;

    if ( s.size() == 1 ) return '0' <= s[0] && s[0] <= '9';

    if ( (s[0] < '0' || s[0] > '9') && s[0] != '-' ){
        return false;
    }

    for ( int i = 1; i < s.size(); i++ ){
        if ( s[i] < '0' || s[i] > '9' ) return false;
    }

    return true;
}

bool ParseInt(const std::string &s, int &value){
    if ( !isInteger(s) || s.size() > size_t(9) ) return false;

    value = stoi(s);
    return true;
}

void LogAppend(const std::string &filename, const std::string &line){
    if ( !persistenceEnabled ) return;

    std::ofstream file(filename, std::ios::app);
    file << line << '\n';
}

std::string HandleCommand(MatchingEngine &engine, const std::string &msg){
    std::vector <std::string> vec = divide(msg);

    if ( vec.empty() ) return "Empty Query";

    if ( vec[0] == "ADD" ){
        Order order;

        if ( vec.size() != 7 ) return "Invalid Order";
        
        if ( !ParseInt(vec[1], order.orderId) ) return "Invalid Order";
        if ( !ParseInt(vec[2], order.price) ) return "Invalid Order";
        if ( !ParseInt(vec[3], order.quantity) ) return "Invalid Order";

        if ( vec[4] != "BUY" && vec[4] != "SELL" ) return "Invalid Order";
        if ( vec[5] != "LIMIT" && vec[5] != "MARKET" ) return "Invalid Order";

        
        order.side = vec[4] == "BUY" ? Side::Buy : Side::Sell;
        order.type = vec[5] == "LIMIT" ? OrderType::Limit : OrderType::Market;
        order.instrument = vec[6];

        auto verdict = engine.AddOrder(order);

        if ( verdict == AddOrderResult::Accepted ){
            LogAppend(logPath, msg);

            return "Accepted";
        }
        if ( verdict == AddOrderResult::DuplicateId ) return "Duplicate Id";

        return "Invalid Order";
    }

    if ( vec[0] == "CANCEL" ){
        int orderId;

        if ( vec.size() != 2 || !ParseInt(vec[1], orderId) ){
            return "Invalid 'Cancel' query";
        }
        
        CancelResult verdict = engine.CancelOrder(orderId);

        if ( verdict == CancelResult::Cancelled ){
            LogAppend(logPath, msg);

            return "Cancelled";
        }
        
        return "Not Found";
    }

    if ( vec[0] == "GET" ){
        int orderId;

        if ( vec.size() != 2 || !ParseInt(vec[1], orderId) ){
            return "Invalid 'Get' query";
        }

        auto order = engine.GetOrder(orderId);

        if ( order == std::nullopt ) return "Not Found";

        return std::string(
            std::to_string(order -> orderId) + " " + 
            std::to_string(order -> price) + " " + 
            std::to_string(order -> quantity) + " " +  
            (order -> side == Side::Buy ? "Buy" : "Sell") + " " + 
            (order -> type == OrderType::Limit ? "Limit" : "Market") + " " + 
            order -> instrument
        );
    }

    if ( vec[0] == "TRADES" ){
        return std::to_string(engine.GetTrades().size());
    }

    return "Invalid Query";
}

bool SendAll(int fd, const void *data, size_t size){
    auto bytes = static_cast <const char*> (data);

    size_t sent = 0;

    while ( sent < size ){
        ssize_t result = send(
            fd,
            bytes + sent,
            size - sent,
            MSG_NOSIGNAL
        );

        if ( result <= 0 ) return false;

        sent += result;
    }
    
    return true;
}

bool RecvLine(int fd, std::string &pending, std::string &line, void *data, size_t size){
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

            return true;
        }
    }
    
    auto bytes = static_cast <char*> (data);

    int total_received = 0;

    while ( true ){
        ssize_t bytes_received = recv(fd, bytes, size, 0);

        if ( bytes_received <= 0 ) return false;

        total_received += bytes_received;

        for ( int i = 0; i < bytes_received; i++ ){
            if ( *(bytes + i) == '\n' ){
                line += pending;
                pending.clear();

                for ( int j = i + 1; j < bytes_received; j++ ){
                    pending.push_back(*(bytes + j));
                }

                return true;
            }

            pending.push_back(*(bytes + i));
        }

        if ( total_received > 1000 ) return false;
    }

    assert(false);
}

bool RecvAll(int fd, void *data, size_t size){
    auto bytes = static_cast <char*> (data);

    int received = 0;

    while ( received < size ){
        ssize_t result = recv(fd, bytes + received, size - received, 0);

        if ( result <= 0 ) return false;

        received += result;
    }

    return true;
}

void HandleClient(int client_fd, MatchingEngine &engine, std::mutex &engine_mutex){
    char buffer[100];

    std::string pending, line;

    while ( true ){
        bool receive_result = RecvLine(
            client_fd,
            pending,
            line,
            buffer,
            sizeof(buffer)
        );

        if ( !receive_result ){
            std::cout << "Client disconnected\n";

            break;
        }

        std::string verdict;

        std::cout << "Line received: " << line << '\n';

        {
            std::lock_guard <std::mutex> lock(engine_mutex);
            verdict = HandleCommand(engine, line);
        }
        
        verdict += '\n';
        line.clear();
        
        if ( !SendAll(client_fd, verdict.data(), verdict.size()) ){
            perror("sending to client");
            break;
        }
    }
        
    close(client_fd);
}

void RestoreLog(const std::string &filename, MatchingEngine &engine){
    if ( !persistenceEnabled ) return; 

    std::ifstream file(filename);

    std::string line;

    while ( std::getline(file, line) ){
        std::istringstream input(line);
        
        std::string type;
        input >> type;

        if ( type == "ADD" ){
            Order order;
            std::string side, type;

            input 
            >> order.orderId 
            >> order.price 
            >> order.quantity 
            >> side 
            >> type
            >> order.instrument;

            order.side = side == "BUY" ? Side::Buy : Side::Sell;
            order.type = type == "LIMIT" ? OrderType::Limit : OrderType::Market;
            
            engine.AddOrder(order);
        } else if ( type == "CANCEL" ){
            int orderId; 
            input >> orderId;

            engine.CancelOrder(orderId);
        }
    }
}

int main(int argc, char *argv[]){
    int server_port;

    if ( argc < 2 || !ParseInt(argv[1], server_port)  ){
        server_port = 4000; // default port
    }

    if ( argc >= 4 && std::string(argv[2]) == "--log" ){
        logPath = argv[3];
    }

    persistenceEnabled = !logPath.empty();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if ( server_fd == -1 ){
        perror("socket");
        return 0;
    }

    int opt = 1;

    setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

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
        return 0;
    }

    int listen_verdict = listen(server_fd, 3);

    if ( listen_verdict != 0 ){
        perror("listen");
        return 0;
    }

    MatchingEngine engine;
    std::mutex engine_mutex;

    RestoreLog(logPath, engine);

    std::cout << "Restore successfull\n";

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
            std::cout << "Client socket failed\n";

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

    close(server_fd);
}