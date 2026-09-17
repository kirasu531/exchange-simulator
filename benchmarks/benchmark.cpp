#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <random>
#include <algorithm>
#include <iomanip>

#include "matching_engine.hpp"
#include "benchmark.hpp"

std::mt19937 rng(1337);

#define rnd(l, r) uniform_int_distribution <int> (l, r)(rng)

void PrintTests(const std::vector <Test> &tests){
    std::cout << "Seed: " << 1337 << '\n';

    std::cout << std::fixed << std::setprecision(3);

    for ( auto &test: tests ){
        std::cout << test.type << ' '
        << test.operations << ' '
        << test.medianTime << "(s) "
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

double getMedian(std::vector <double> times){
    std::sort(times.begin(), times.end());

    int size = times.size();

    if ( size % 2 == 0 ){
        return (times[size / 2] + times[size / 2 - 1]) / 2;
    }

    return times[size / 2];
}

double RunMostlyResting(int runs){
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

    std::vector <double> times;
    
    for ( int runId = 0; runId < runs; runId++ ){
        MatchingEngine engine;
        ResetTime();

        for ( int orderId = 0; orderId < operationCount; orderId++ ){ 
            engine.AddOrder(orders[orderId]);
        }

        times.push_back(getExecTime());
        
        if ( runId == 0 ){
            totalTrades = engine.GetTrades().size();
        }
    }

    return getMedian(times);
}

double RunManyInstruments(int runs){
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

    std::vector <double> times;

    for ( int runId = 0; runId < runs; runId++ ){
        MatchingEngine engine;
        ResetTime();

        for ( int orderId = 0; orderId < operationCount; orderId++ ){ 
            engine.AddOrder(orders[orderId]);
        }

        times.push_back(getExecTime());
        
        if ( runId == 0 ){
            totalTrades = engine.GetTrades().size();
        }
    }

    return getMedian(times);
}

double RunCancelHeavy(int runs){
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

    std::vector <double> times;

    for ( int runId = 0; runId < runs; runId++ ){
        MatchingEngine engine;
        ResetTime();

        for ( int orderId = 0; orderId < operationCount; orderId++ ){
            engine.AddOrder(orders[orderId]);
        }

        for ( int orderId = 0; orderId < operationCount; orderId++ ){
            engine.CancelOrder(orderId);
        }

        times.push_back(getExecTime());

        if ( runId == 0 ){
            totalTrades = engine.GetTrades().size();
        }
    }
    
    return getMedian(times);
}

double RunRandom(int runs){
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

    std::vector <double> times;

    for ( int runId = 0; runId < runs; runId++ ){
        MatchingEngine engine;
        ResetTime();

        for ( int orderId = 0; orderId < operationCount; orderId++ ){ 
            engine.AddOrder(orders[orderId]);
        }

        times.push_back(getExecTime());

        if ( runId == 0 ){
            totalTrades = engine.GetTrades().size();
        }
    }

    return getMedian(times);
}

double RunMostlyCrossing(int runs){
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

    std::vector <double> times;

    for ( int runId = 0; runId < runs; runId++ ){
        MatchingEngine engine;
        ResetTime();

        for ( int orderId = 0; orderId < operationCount; orderId++ ){
            engine.AddOrder(orders[orderId]);
        }

        times.push_back(getExecTime());

        if ( runId == 0 ){
            totalTrades = engine.GetTrades().size();
        }
    }

    return getMedian(times);
}

double RunLargeSweep(int runs){
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

    std::vector <double> times;

    for ( int runId = 0; runId < runs; runId++ ){
        MatchingEngine engine;

        for ( int orderId = 0; orderId < operationCount - 5; orderId++ ){
            engine.AddOrder(orders[orderId]);
        }

        ResetTime();

        for ( int orderId = operationCount - 5; orderId < operationCount; orderId++ ){
            engine.AddOrder(orders[orderId]);
        }

        times.push_back(getExecTime());

        if ( runId == 0 ){
            totalTrades = engine.GetTrades().size();
        }
    }

    return getMedian(times);
}

int main(int argc, char* argv[]){
    if ( argc >= 3 ){
        std::string type;

        type = argv[1];
        operationCount = std::stoi(argv[2]);
        int runs = 1;

        if ( argc > 3 ) runs = std::stoi(argv[3]);

        orders.resize(operationCount);

        std::vector <Test> tests;

        if ( type == "Resting" ){
            tests.push_back(Test{
                .type = "Resting",
                .operations = operationCount,
                .medianTime = RunMostlyResting(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });
        }

        if ( type == "CancelHeavy" ){
            tests.push_back(Test{
                .type = "CancelHeavy",
                .operations = operationCount * 2,
                .medianTime = RunCancelHeavy(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });
        }

        if ( type == "Random" ){
            tests.push_back(Test{
                .type = "Random",
                .operations = operationCount,
                .medianTime = RunRandom(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });
        }

        if ( type == "MostlyCrossing" ){
            tests.push_back(Test{
                .type = "MostlyCrossing",
                .operations = operationCount,
                .medianTime = RunMostlyCrossing(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });
        }

        if ( type == "LargeSweep" ){
            tests.push_back(Test{
                .type = "LargeSweep",
                .operations = 5,
                .medianTime = RunLargeSweep(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });
        }

        if ( type == "ManyInstruments" ){
            tests.push_back(Test{
                .type = "ManyInstruments",
                .operations = operationCount,
                .medianTime = RunManyInstruments(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });
        }

        PrintTests(tests);
    } else{
        std::vector <Test> tests;

        for ( auto curN: {1e4, 1e5, 5e5} ){
            orders.clear();
            orders.resize(operationCount = curN);
            
            int runs = std::min(20, (int)(1e6 / curN * 5));

            tests.push_back(Test{
                .type = "Resting",
                .operations = operationCount,
                .medianTime = RunMostlyResting(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });

            tests.push_back(Test{
                .type = "CancelHeavy",
                .operations = operationCount * 2,
                .medianTime = RunCancelHeavy(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });

            tests.push_back(Test{
                .type = "ManyInstruments",
                .operations = operationCount,
                .medianTime = RunManyInstruments(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });

            tests.push_back(Test{
                .type = "Random",
                .operations = operationCount,
                .medianTime = RunRandom(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });

            tests.push_back(Test{
                .type = "MostlyCrossing",
                .operations = operationCount,
                .medianTime = RunMostlyCrossing(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });

            tests.push_back(Test{
                .type = "LargeSweep",
                .operations = 5,
                .medianTime = RunLargeSweep(runs),
                .totalRuns = runs,
                .trades = totalTrades
            });
        }

        std::sort(begin(tests), end(tests), [&](Test &first, Test &second){
            if ( first.type == second.type ){
                return first.operations < second.operations;
            }

            return first.type < second.type;
        });

        PrintTests(tests);
    }
}