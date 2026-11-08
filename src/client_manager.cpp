#include "client_manager.hpp"

#include <sys/epoll.h>
#include <unistd.h>
#include <cstdio>
#include <iostream>

ClientManager::ClientManager(int epfd) : epfd(epfd) {}

void ClientManager::Register(int fd){
    connections[fd] = Connection();
}

Connection* ClientManager::Find(int fd){
    auto it = connections.find(fd);

    if ( it == connections.end() ) return nullptr;

    return &it -> second;
}

bool ClientManager::Subscribe(int fd, Connection &client, const std::string &instrument){
    if ( subscribers[instrument].contains(fd) ) return false;

    subscribers[instrument].insert(fd);
    client.subscriptions.insert(instrument);

    return true;
}

bool ClientManager::Unsubscribe(int fd, Connection &client, const std::string &instrument){
    auto sub_it = subscribers.find(instrument);

    if ( sub_it == subscribers.end() || !sub_it -> second.contains(fd) ){
        return false;
    }

    sub_it -> second.erase(fd);
    client.subscriptions.erase(instrument);

    if ( sub_it -> second.empty() ){
        subscribers.erase(sub_it);
    }

    return true;
}

void ClientManager::PublishTrades(const std::string &instrument, const std::vector <Trade> &trades){
    auto sub_it = subscribers.find(instrument);

    if ( sub_it != subscribers.end() ){
        for ( auto &client_fd: sub_it -> second ){
            auto conn_it = connections.find(client_fd);

            if ( conn_it == connections.end() ) continue;

            bool alive = true;

            for ( auto &trade: trades ){
                conn_it -> second.output_buffer += "EVENT " + trade.to_string() + '\n';

                if ( conn_it -> second.pendingBytes() > PENDING_OUTPUT_LIMIT ){
                    disconnect_queue.push_back(client_fd);
                    alive = false;

                    break;
                }
            }

            if ( alive ) epoll_update_queue.push_back(client_fd);
        }
    }
}

void ClientManager::QueueEpollUpdate(int fd){
    epoll_update_queue.push_back(fd);
}

void ClientManager::QueueDisconnect(int fd){
    disconnect_queue.push_back(fd);
}

bool ClientManager::SyncEpollInterest(int fd){
    if ( !connections.contains(fd) ) return false;

    auto &client = connections[fd];
    bool alive = true;

    bool has_pending_output = client.hasPendingData();

    if ( has_pending_output != client.watching_output ){
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;

        if ( has_pending_output ) ev.events |= EPOLLOUT;

        if ( epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1 ){
            perror("epoll_ctl MOD");
            alive = false;
        }

        client.watching_output = has_pending_output;
    }

    if ( !has_pending_output ){
        client.output_buffer.clear();
        client.sent_offset = 0;
    }

    return alive;
}

void ClientManager::FlushEpollUpdates(){
    for ( auto &client_fd: epoll_update_queue ){
        if ( !SyncEpollInterest(client_fd) ){
            disconnect_queue.push_back(client_fd);
        }
    }

    epoll_update_queue.clear();
}

void ClientManager::FlushDisconnects(){
    for ( auto &client_fd: disconnect_queue ){
        auto it = connections.find(client_fd);

        if ( it == connections.end() ) continue;

        auto &client = it -> second;
        auto subscriptions_copy = client.subscriptions;

        for ( auto &instrument: subscriptions_copy ){
            Unsubscribe(client_fd, client, instrument);
        }

        if ( epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, nullptr) == -1 ){
            perror("epoll_ctl DEL");
        }

        if ( close(client_fd) == -1 ) perror("close client");

        std::cout << "Client disconnected/closed\n";
        connections.erase(it);
    }

    disconnect_queue.clear();
}
