#pragma once
#include "order_book.hpp"

#include <string>
#include <map>
#include <set>
#include <optional>

struct MatchingEngine{
    std::map <std::string, OrderBook> groups;
    std::map <int, std::string> type;
    std::set <int> used_Ids;
    int nextSequence = 0;

    void AddOrder(const Order &newOrder);
    void CancelOrder(int Id);
    std:: optional <Order> GetOrder(int Id);
    void PrintTrades();
};