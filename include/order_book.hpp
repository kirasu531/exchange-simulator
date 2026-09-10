#pragma once
#include "trade.hpp"

#include <iostream>
#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <optional>

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
    std::set <Order, ByDecreasingOrder> buy_list;
    std::set <Order, ByIncreasingOrder> sell_list;
    std::map <int, Order> all_orders;
    std::map <int, Order> active_orders;
    std::vector <Trade> trades;

    AddOrderResult AddOrder(const Order &newOrder, int &nextTradeNumber);
    CancelResult RemoveOrder(int Id);
    std::optional <Order> GetOrder(int Id);
    void PrintLog();
};