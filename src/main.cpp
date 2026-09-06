#include "matching_engine.hpp"

#include <string>
#include <iostream>

int cntTime = 0;

int getTime(){ return cntTime++; }

int main(){
    int t; std::cin >> t;

    MatchingEngine engine;

    while ( t-- ){
        std::string op; std::cin >> op;

        if ( op == "add" ){
            int orderId;
            int arrivalTime = getTime();
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
                .arrivalTime = arrivalTime, 
                .price = price,
                .quantity = quantity,
                .side = side == "buy" ? Side::Buy : Side::Sell,
                .instrument = instrument
            });
        } else if ( op == "rmv" ){
            int Id; 
            
            std::cin >> Id;

            engine.CancelOrder(Id);
        } else if ( op == "get" ){
            int Id; 
            
            std::cin >> Id;

            auto curOrder = engine.GetOrder(Id);

            if ( curOrder ){
                engine.GetOrder(Id) -> print();
            }
        }
    }

    engine.PrintTrades();
}