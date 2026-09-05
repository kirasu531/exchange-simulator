#pragma once
#include <string>

struct Trade{
    int buyerId = -1;
    int sellerId = -1;
    std::string instrument = "null";
    int price = 0;
    int quantity = 0;
};