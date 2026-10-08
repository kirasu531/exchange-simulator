#include "matching_engine.hpp"
#include "network_utils.hpp"

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
#include <thread>

#define rnd(l, r) uniform_int_distribution <int> (l, r)(rng)

std::mt19937 rng(1337);

int operationCount, totalTrades = 0, client_fd;

std::chrono::time_point<std::chrono::steady_clock> start, latency_start;

std::vector <std::string> orders;
std::string pending, line;

std::vector <double> latencies;

struct Test{
    std::string type;
    int operations;
    int trades;
    double time;
    int throughput; // ops/s
    double avgLatency; // ms
    double p50; 
    double p95; 
    double p99; 

    void print() const{
        std::cout
        << type << ' '
        << operations << ' '
        << trades << ' '
        << time << ' ' 
        << throughput << ' '
        << avgLatency << ' '
        << p50 << ' '
        << p95 << ' '
        << p99 << ' ';
    }
};

void ResetRng(){ 
    rng = std::mt19937(1337);
}

void ResetTime(auto &start){
    start = std::chrono::steady_clock::now();
}

double GetRuntimeSec(const auto &start){
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

void RoundTrip(const std::string &command, void *data, size_t size, const std::string &verdict){
    ResetTime(latency_start);

    if ( SendAll(client_fd, command.data(), command.size()) == IOResult::Error ){
        perror("Sending to server");
        exit(1);
    }

    auto receive_result = RecvLine(
        client_fd,
        pending,
        line,
        data,
        size
    );

    if ( receive_result == RecvLineResult::Closed ){
        std::cout << "Server closed\n";
        exit(1);
    }

    if ( receive_result == RecvLineResult::IOError ){
        perror("receive from server");
        exit(1);
    }

    if ( line != verdict ){
        std::cout << "Wrong verdict\n";
        exit(1);
    }

    latencies.push_back(GetRuntimeSec(latency_start) * 1'000);
}

void GetTrades(void *data, size_t size){
    std::string command = "TRADES\n";

    pending.clear();
    line.clear();


    if ( SendAll(client_fd, command.data(), command.size()) == IOResult::Error ){
        perror("Sending to server");
        exit(1);
    }

    auto receive_result = RecvLine(client_fd, pending, line, data, size);
    
    if ( receive_result == RecvLineResult::Closed ){
        std::cout << "Server closed\n";
        exit(1);
    }

    if ( receive_result == RecvLineResult::IOError ){
        perror("receiving from server");
        exit(1);
    }

    totalTrades = std::stoi(line);
}

double RunMostlyResting(){
    ResetRng();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        int side = std::rnd(0, 1), price = side ? 2 : 1;

        orders[orderId] = AddOrderToStr(Order{
            .orderId = orderId,
            .price = price,
            .quantity = 1,
            .side = side ? Side::Sell : Side::Buy,
            .type = OrderType::Limit,
            .instrument = "BTC"
        });
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime(start);

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(orders[orderId], data, sizeof(data), "Accepted");
    }

    double runtime = GetRuntimeSec(start);
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
        orders[orderId] = AddOrderToStr(Order{
            .orderId = orderId,
            .price = 1,
            .quantity = 1,
            .side = Side::Sell,
            .type = OrderType::Limit,
            .instrument = instrument
        });

        FindNext(instrument);
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime(start);

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(orders[orderId], data, sizeof(data), "Accepted");
    }

    double runtime = GetRuntimeSec(start);
    GetTrades(data, sizeof(data));

    return runtime;
}

double RunCancelHeavy(){
    ResetRng();

    std::vector<std::string> cancel(operationCount);

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        orders[orderId] = AddOrderToStr(Order{
            .orderId = orderId,
            .price = std::rnd(1, 100000),
            .quantity = 1,
            .side = Side::Sell,
            .type = OrderType::Limit,
            .instrument = "BTC"
        });

        cancel[orderId] = CancelToStr(orderId);
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime(start);

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(orders[orderId], data, sizeof(data), "Accepted");
    }

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(cancel[orderId], data, sizeof(data), "Cancelled");
    }

    double runtime = GetRuntimeSec(start);
    GetTrades(data, sizeof(data));
    
    return runtime;
}

double RunRandom(){
    ResetRng();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        orders[orderId] = AddOrderToStr(Order{
            .orderId = orderId, 
            .price = std::rnd(1, 100000),
            .quantity = std::rnd(1, 100000),
            .side = std::rnd(0, 1) ? Side::Sell : Side::Buy,
            .type = OrderType::Limit,
            .instrument = "BTC"
        });
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime(start);

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(orders[orderId], data, sizeof(data), "Accepted");
    }

    double runtime = GetRuntimeSec(start);
    GetTrades(data, sizeof(data));

    return runtime;
}

double RunMostlyCrossing(){
    ResetRng();

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        int side = std::rnd(0, 1);

        orders[orderId] = AddOrderToStr(Order{
            .orderId = orderId,
            .price = side ? 5 : 10,
            .quantity = std::rnd(1, 100000),
            .side = side ? Side::Sell : Side::Buy,
            .type = OrderType::Limit,
            .instrument = "BTC"
        });
    }

    char data[100];
    pending.clear();
    line.clear();

    ResetTime(start);

    for ( int orderId = 0; orderId < operationCount; orderId++ ){
        RoundTrip(orders[orderId], data, sizeof(data), "Accepted");
    }

    double runtime = GetRuntimeSec(start);
    GetTrades(data, sizeof(data));

    return runtime;
}

double RunTest(const int &testId){
    if ( testId == 0 ) return RunMostlyResting();
    if ( testId == 1 ) return RunCancelHeavy();
    if ( testId == 2 ) return RunRandom();
    if ( testId == 3 ) return RunMostlyCrossing();
    if ( testId == 4 ) return RunManyInstruments();
    
    return -1;
}

double GetLatency(int percentage){
    int idx = ((int)latencies.size() * percentage + 99) / 100;

    return latencies[idx - 1];
}

int main(int argc, char *argv[]){
    int interval_time = 1000;

    if ( argc < 3 ){
        std::cout << "Usage: " << argv[0] << " " << "<workload> <operations>\n";

        return 1;
    }

    if ( argc >= 4 ){
        interval_time = std::stoi(argv[3]) * 2;
    }

    std::string type;

    type = argv[1];
    operationCount = std::stoi(argv[2]);

    orders.resize(operationCount);
    latencies.reserve(operationCount * 2);

    int server_port = 4000;

    client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if ( client_fd < 0 ){
        perror("socket");
        return 1;
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

        return 1;
    }

    std::vector <std::string> workloads = {
        "Resting",
        "CancelHeavy",
        "Random",
        "MostlyCrossing",
        "ManyInstruments"
    };

    int idx = -1;

    for ( int i = 0; size_t(i) < workloads.size(); i++ ){
        if ( type == workloads[i] ) idx = i;
    }

    if ( idx == -1 ){
        std::cout << "Invalid <workload> was provided\n";
        close(client_fd);

        return 1;
    }
    
    Test test = Test{
        .type = type,
        .operations = operationCount,
        .time = RunTest(idx)
    };

    test.trades = totalTrades;

    if ( type == "CancelHeavy" ){
        test.operations = operationCount * 2;
    }

    test.throughput = (int)(test.operations / test.time);

    { // calculate latency
        sort(latencies.begin(), latencies.end());

        double total = 0;

        for ( auto &time: latencies ){
            total += time;
        }

        test.avgLatency = total / (double)latencies.size();
        test.p50 = GetLatency(50);
        test.p95 = GetLatency(95);
        test.p99 = GetLatency(99);
    }

    std::cout << std::fixed << std::setprecision(3);
    test.print();

    std::this_thread::sleep_for(std::chrono::milliseconds(interval_time));

    std::string qry = "STATS\n";
    IOResult send_result = SendAll(client_fd, qry.data(), qry.size());

    if ( send_result == IOResult::Error ){
        perror("send");
        return 1;
    }

    std::string pending, line;
    char data[100];

    RecvLine(client_fd, pending, line, data, sizeof(data));

    std::cout << line << '\n';
    
    close(client_fd);
}