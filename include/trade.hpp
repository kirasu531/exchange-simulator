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

    void printTrade(){
        std::cout << tradeSequence << ' ' 
        << buyerId << ' ' 
        << sellerId << ' ' 
        << instrument << ' ' 
        << price << ' ' 
        << quantity << '\n';
    }
};