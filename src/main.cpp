#include "matching_engine.hpp"

#include <string>
#include <iostream>

int main(){
    int t; std::cin >> t;

    MatchingEngine engine;

    while ( t-- ){
        std::string op; std::cin >> op;

        if ( op == "add" ){
            int orderId;
            int price;
            int quantity;
            std::string side;
            std::string instrument;

            std::cin >> orderId >> 
            price >> 
            quantity >>
            side >>
            instrument;  

            engine.AddOrder(Order{
                .orderId = orderId,
                .price = price,
                .quantity = quantity,
                .side = side == "buy" ? Side::Buy : Side::Sell,
                .type = OrderType::Limit,
                .instrument = instrument
            });
        } else if ( op == "rmv" ){
            int orderId;

            std::cin >> orderId;

            engine.CancelOrder(orderId);
        } else if ( op == "get" ){
            int orderId;

            std::cin >> orderId;

            auto curOrder = engine.GetOrder(orderId);

            if ( curOrder ){
                curOrder -> print();
            }
        }
    }

    engine.PrintTrades();
}