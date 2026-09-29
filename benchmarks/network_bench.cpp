#include "network_bench.hpp"
#include "matching_engine.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <chrono>
#include <random>
#include <iomanip>
#include <cassert>
#include <vector>
#include <string>

std::mt19937 rng(1337);

#define rnd(l, r) uniform_int_distribution <int> (l, r)(rng)

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

    while ( true ){
        ssize_t bytes_received = recv(fd, bytes, size, 0);

        if ( bytes_received <= 0 ) return false;

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
    }

    assert(false); // function should never reach it
}

void PrintTests(const std::vector <Test> &tests){
    std::cout << std::fixed << std::setprecision(3);

    for ( auto &test: tests ){
        std::cout << test.type << ' '
        << test.operations << ' '
        << test.time << "(s) "
        << test.totalRuns << ' '
        << test.trades << '\n';
    }
}

int operationCount, totalTrades = 0;

std::chrono::time_point<std::chrono::steady_clock> start;

std::vector <Order> orders;

void ResetRng(){ 
    rng = std::mt19937(1337);
}

void ResetTime(){
    start = std::chrono::steady_clock::now();
}

double getExecTime(){
    auto duration = std::chrono::steady_clock::now() - start;

    return std::chrono::duration <double> (duration).count();
}

std::string AddOrderToStr(const Order &order){
    return "ADD " + std::to_string(order.orderId) 
    + " " + std::to_string(order.price) 
    + " " + std::to_string(order.quantity) 
    + " " + (order.side == Side::Buy ? "BUY" : "SELL") 
    + " " + (order.type == OrderType::Limit ? "LIMIT" : "MARKET") 
    + " " + order.instrument + '\n';
}

std::string CancelToStr(const int &orderId){
    return "CANCEL " + std::to_string(orderId) + '\n';
}

std::string pending, line;

int client_fd;

void RoundTrip(const std::string &command, void *data, size_t size, const std::string verdict){
    if ( !SendAll(client_fd, command.data(), command.size()) ){
        perror("Sending to server");
        exit(1);
    }

    bool receive_result = RecvLine(
        client_fd,
        pending,
        line,
        data,
        size
    );

    if ( !receive_result ){
        std::cout << "Server closed unexpectedly\n";
        exit(1);
    }

    if ( line != verdict ){
        std::cout << "Wrong verdict\n";
        exit(1);
    }
}

void GetTrades(void *data, size_t size){
    std::string command = "TRADES\n";

    pending.clear();
    line.clear();


    if ( !SendAll(client_fd, command.data(), command.size()) ){
        perror("Sending to server");
        exit(1);
    }
    
    if ( !RecvLine(client_fd, pending, line, data, size) ){
        std::cout << "Server closed unexpectedly\n";
        exit(1);
    }

    totalTrades = std::stoi(line);
}

double RunMostlyResting(){
    ResetRng();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        int side = std::rnd(0, 1), price = side ? 2 : 1;

        orders[orderId] = Order{
            .orderId = orderId,
            .price = price,
            .quantity = 1,
            .side = side ? Side::Sell : Side::Buy,
            .type = OrderType::Limit,
            .instrument = "BTC"
        };
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(AddOrderToStr(orders[orderId]), data, sizeof(data), "Accepted");
    }

    double runtime = getExecTime();
    GetTrades(data, sizeof(data));

    return runtime;
}

double RunManyInstruments(){
    ResetRng();

    std::string instrument = "ADACB"; // expected average size of instruments [5, 8]

    auto FindNext = [&](std::string &str){
        for ( int i = (int)str.size() - 1; i >= 0; i-- ){
            if ( instrument[i] != 'D' ){
                instrument[i] += 1;

                return;
            }    
        }

        for ( auto &x: str ) x = std::rnd(0, 3) + 'A';

        str += std::rnd(0, 3) + 'A';
    };

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        orders[orderId] = Order{
            .orderId = orderId,
            .price = 1,
            .quantity = 1,
            .side = Side::Sell,
            .type = OrderType::Limit,
            .instrument = instrument
        };

        FindNext(instrument);
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(AddOrderToStr(orders[orderId]), data, sizeof(data), "Accepted");
    }

    double runtime = getExecTime();
    GetTrades(data, sizeof(data));

    return runtime;
}

double RunCancelHeavy(){
    ResetRng();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        orders[orderId] = Order{
            .orderId = orderId,
            .price = std::rnd(1, 100000),
            .quantity = 1,
            .side = Side::Sell,
            .type = OrderType::Limit,
            .instrument = "BTC"
        };
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(AddOrderToStr(orders[orderId]), data, sizeof(data), "Accepted");
    }

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(CancelToStr(orderId), data, sizeof(data), "Cancelled");
    }

    double runtime = getExecTime();
    GetTrades(data, sizeof(data));
    
    return runtime;
}

double RunRandom(){
    ResetRng();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        orders[orderId] = Order{
            .orderId = orderId,
            .price = std::rnd(1, 100000),
            .quantity = std::rnd(1, 100000),
            .side = std::rnd(0, 1) ? Side::Sell : Side::Buy,
            .type = OrderType::Limit,
            .instrument = "BTC"
        };
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(AddOrderToStr(orders[orderId]), data, sizeof(data), "Accepted");
    }

    double runtime = getExecTime();
    GetTrades(data, sizeof(data));

    return runtime;
}

double RunMostlyCrossing(){
    ResetRng();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        int side = std::rnd(0, 1);

        orders[orderId] = Order{
            .orderId = orderId,
            .price = side ? 5 : 10,
            .quantity = std::rnd(1, 100000),
            .side = side ? Side::Sell : Side::Buy,
            .type = OrderType::Limit,
            .instrument = "BTC"
        };
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(AddOrderToStr(orders[orderId]), data, sizeof(data), "Accepted");
    }

    double runtime = getExecTime();
    GetTrades(data, sizeof(data));

    return runtime;
}

double RunLargeSweep(){
    ResetRng();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        if ( orderId < operationCount - 5 ){
            orders[orderId] = Order{
                .orderId = orderId,
                .price = 5,
                .quantity = 1,
                .side = Side::Sell,
                .type = OrderType::Limit,
                .instrument = "BTC"
            };
        } else{
            orders[orderId] = Order{
                .orderId = orderId,
                .price = 10,
                .quantity = std::rnd(operationCount / 5, operationCount / 4),
                .side = Side::Buy,
                .type = OrderType::Limit,
                .instrument = "BTC"
            };
        }
    }

    char data[100];
    pending.clear();
    line.clear();

    for ( int orderId = 0; orderId < operationCount - 5; orderId++ ){
        RoundTrip(AddOrderToStr(orders[orderId]), data, sizeof(data), "Accepted");
    }

    ResetTime();

    for ( int orderId = operationCount - 5; orderId < operationCount; orderId++ ){
        RoundTrip(AddOrderToStr(orders[orderId]), data, sizeof(data), "Accepted");
    }

    double runtime = getExecTime();
    GetTrades(data, sizeof(data));

    return runtime;
}

double RunTest(int testId){
    if ( testId == 0 ) return RunMostlyResting();
    if ( testId == 1 ) return RunCancelHeavy();
    if ( testId == 2 ) return RunRandom();
    if ( testId == 3 ) return RunMostlyCrossing();
    if ( testId == 4 ) return RunLargeSweep();
    if ( testId == 5 ) return RunManyInstruments();
    
    return -1;
}

int main(int argc, char *argv[]){
    int server_port = 4000;

    client_fd = socket(AF_INET, SOCK_STREAM, 0);

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

    if ( argc < 3 ){
        std::cout << "Usage: " << argv[0] << " " << "<workload> <operations>\n";
        
        close(client_fd);
        return 0;
    }

    std::string type;

    type = argv[1];
    operationCount = std::stoi(argv[2]);

    orders.resize(operationCount);

    std::vector <std::string> workloads = {
        "Resting",
        "CancelHeavy",
        "Random",
        "MostlyCrossing",
        "LargeSweep",
        "ManyInstruments"
    };

    int idx = -1;

    for ( int i = 0; size_t(i) < workloads.size(); i++ ){
        if ( type == workloads[i] ) idx = i;
    }

    if ( idx == -1 ){
        std::cout << "Invalid <workload> was provided\n";
        close(client_fd);

        return 0;
    }
    
    Test test = Test{
        .type = type,
        .time = RunTest(idx),
        .totalRuns = 1,
        .trades = totalTrades
    };

    if ( type == "CancelHeavy" ){
        test.operations = operationCount * 2;
    } else if ( type == "LargeSweep" ){
        test.operations = 5;
    } else{
        test.operations = operationCount;
    }
    
    PrintTests(std::vector <Test> {test});
    close(client_fd);
}