#pragma once

#include "order_book.hpp"

#include <string>
#include <map>
#include <optional>
#include <vector>
#include <unordered_map>

struct MatchingEngine{
    private:
        std::map <std::string, OrderBook> books;
        std::unordered_map <int, std::string> order_instruments;
        int nextSequence = 0;
        int nextTradeNumber = 0;

    public:
        AddOrderResult AddOrder(const Order &newOrder);
        CancelResult CancelOrder(int orderId);
        std:: optional <Order> GetOrder(int orderId);
        std:: vector <Trade> GetTrades();
        void PrintTrades();
        size_t GetTradeCount(const std::string &instrument);
        std::vector <Trade> GetTradesFrom(const std::string &instrument, size_t start);
};
