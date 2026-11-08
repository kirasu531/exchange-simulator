#pragma once

#include "network_utils.hpp"
#include "trade.hpp"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class ClientManager {
private:
    int epfd;

    std::unordered_map <int, Connection> connections;
    std::unordered_map <std::string, std::set <int>> subscribers;

    std::vector <int> disconnect_queue;
    std::vector <int> epoll_update_queue;

public:
    explicit ClientManager(int epfd);

    void Register(int fd);
    Connection* Find(int fd);

    bool Subscribe(int fd, Connection &client, const std::string &instrument);
    bool Unsubscribe(int fd, Connection &client, const std::string &instrument);

    void PublishTrades(
        const std::string &instrument,
        const std::vector <Trade> &trades
    );

    void QueueEpollUpdate(int fd);
    void QueueDisconnect(int fd);

    bool SyncEpollInterest(int fd);

    void FlushEpollUpdates();
    void FlushDisconnects();
};
