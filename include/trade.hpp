#pragma once

#include <string>
#include <iostream>

struct Trade{
    int tradeSequence = 0;
    int buyerId = -1;
    int sellerId = -1;
    std::string instrument = "null";
    int price = 0;
    int quantity = 0;

    void printTrade() const{
        std::cout << tradeSequence << ' ' 
        << buyerId << ' ' 
        << sellerId << ' ' 
        << instrument << ' ' 
        << price << ' ' 
        << quantity << '\n';
    }

    std::string to_string() const{
        return "TRADE " + instrument + " "
        + std::to_string(tradeSequence) + " "
        + std::to_string(buyerId) + " "
        + std::to_string(sellerId) + " "
        + std::to_string(price) + " "
        + std::to_string(quantity);
    }
};