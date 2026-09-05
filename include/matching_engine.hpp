#pragma once
#include "order_book.hpp"

struct MatchingEngine{
    std::map <std::string, OrderBook> groups;
    std::map <int, std::string> type; 

    MatchingEngine() {}

    void AddOrder(const Order &newOrder);
    void CancelOrder(const int &Id);
};