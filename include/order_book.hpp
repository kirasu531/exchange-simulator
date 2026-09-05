#pragma once
#include "trade.hpp"

#include <iostream>
#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <string>

enum class Side{
    Buy,
    Sell
};

struct Order{
    int orderId = -1;
    int arrivalTime = 0;
    int price = 0;
    int quantity = 0;
    Side side;
    std::string instrument = "null";

    void print();
};

bool operator <(const Order &First, const Order &Second);

struct OrderBook{
    std::set <Order> buy_list, sell_list;
    std::map <int, Order> all_orders, active_orders;
    std::vector <Trade> trades;

    void AddOrder(const Order &newOrder);
    void RemoveOrder(int Id);
    Order GetOrder(int Id);
    void PrintLog();
};