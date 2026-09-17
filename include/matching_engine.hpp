#pragma once
#include "order_book.hpp"

#include <string>
#include <map>
#include <set>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct MatchingEngine{
    private:
        std::map <std::string, OrderBook> groups;
        std::unordered_map <int, std::string> type;
        int nextSequence = 0;
        int nextTradeNumber = 0;

    public:
        AddOrderResult AddOrder(const Order &newOrder);
        CancelResult CancelOrder(int Id);
        std:: optional <Order> GetOrder(int Id);
        std:: vector <Trade> GetTrades();
        void PrintTrades();
};