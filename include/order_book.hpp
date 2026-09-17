#pragma once
#include "trade.hpp"

#include <iostream>
#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <optional>
#include <queue>
#include <unordered_set>
#include <unordered_map>

enum class Side{
    Buy,
    Sell
};

enum class OrderType{
    Limit,
    Market
};

enum class AddOrderResult{
    Accepted,
    DuplicateId,
    InvalidOrder
};

enum class CancelResult{
    Cancelled, 
    NotFound
};

struct Order{
    int orderId = -1;
    int arrivalSequence = 0;
    int price = 0;
    int quantity = 0;
    Side side;
    OrderType type;
    std::string instrument;

    void print();
};

bool IsValidOrder(const Order &order);

struct ByIncreasingOrder{
    bool operator ()(const Order &First, const Order &Second) const;
};

struct ByDecreasingOrder{
    bool operator ()(const Order &First, const Order &Second) const;
};

bool operator ==(const Order &First, const Order &Second);

struct OrderBook{
    private:
        std::map <int, std::queue <Order>> buy_list;
        std::map <int, std::queue <Order>> sell_list;
        std::unordered_map <int, Order> active_orders;
        std::unordered_set <int> canceled_orders;
        std::vector <Trade> trades;

    public:
        AddOrderResult AddOrder(const Order &newOrder, int &nextTradeNumber);
        CancelResult RemoveOrder(int Id);
        std::optional <Order> GetOrder(int Id);
        std::vector <Trade> GetTrades();
        void PrintLog();
};