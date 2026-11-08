#include "persistent_log.hpp"
#include "network_utils.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

PersistentLog::PersistentLog(const std::string &path, int sync_interval_ms) :
    path(path),
    sync_interval_ms(sync_interval_ms)
{}

void PersistentLog::SyncLoop(){
    auto next_sync_time = std::chrono::steady_clock().now();

    while ( sync_running ){
        next_sync_time += std::chrono::milliseconds(sync_interval_ms);
        std::this_thread::sleep_until(next_sync_time);

        std::lock_guard <std::mutex> lock(mutex);

        if ( !dirty ) continue;

        while ( fdatasync(fd) == -1 ){
            if ( errno == EINTR ) continue;

            perror("sync");
            return;
        }

        dirty = false;
    }
}

bool PersistentLog::Open(){
    enabled = !path.empty();

    sync_running = (sync_interval_ms > 0);

    if ( enabled ){
        fd = open(
            path.c_str(),
            O_WRONLY | O_CREAT | O_APPEND,
            0644
        );

        if ( fd == -1 ){
            perror("file open");
            return false;
        }
    }

    return true;
}

void PersistentLog::Replay(MatchingEngine &engine){
    if ( !enabled ) return;

    std::ifstream file(path);

    std::string line;

    while ( std::getline(file, line) ){
        if ( file.eof() ) continue; // every valid line ends with '\n'

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

            if (      side == "BUY"  ) order.side = Side::Buy;
            else if ( side == "SELL" ) order.side = Side::Sell;
            else continue;

            if (      order_type == "LIMIT"  ) order.type = OrderType::Limit;
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

    if ( file.is_open() ){
        std::cout << "Restore successful\n";
    } else{
        std::cout << "No log found\n";
    }
}

void PersistentLog::StartSync(){
    if ( !sync_running || sync_thread.joinable() ) return;

    sync_thread = std::thread(
        &PersistentLog::SyncLoop,
        this
    );
}

void PersistentLog::StopSync(){
    sync_running = false;

    if ( sync_thread.joinable() ){
        sync_thread.join();
    }
}

bool PersistentLog::Append(const std::string &line){
    if ( !enabled ) return true;

    std::string record = line + '\n';

    std::lock_guard <std::mutex> lock(mutex);

    if ( WriteAll(fd, record.data(), record.size()) == IOResult::Error ){
        perror("write log");
        return false;
    }

    dirty = true;
    return true;
}

void PersistentLog::Close(){
    if ( fd == -1 ) return;

    close(fd);
    fd = -1;
}

PersistentLog::~PersistentLog(){
    StopSync();
    Close();
}
