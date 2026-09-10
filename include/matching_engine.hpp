#pragma once
#include "order_book.hpp"

#include <string>
#include <map>
#include <set>
#include <optional>
#include <vector>

struct MatchingEngine{
    std::map <std::string, OrderBook> groups;
    std::map <int, std::string> type;
    std::set <int> used_Ids;
    int nextSequence = 0;
    int nextTradeNumber = 0;

    AddOrderResult AddOrder(const Order &newOrder);
    CancelResult CancelOrder(int Id);
    std:: optional <Order> GetOrder(int Id);
    std:: vector <Trade> GetTrades();
    void PrintTrades();
};