#include "order_book.hpp"

#include <string>
#include <iostream>

int cntTime = 0;

int getTime(){ return cntTime++; }

int main(){
    int t; std::cin >> t;

    OrderBook exchange;

    while ( t-- ){
        std::string op; std::cin >> op;

        if ( op == "add" ){
            std::string type; int id, price, quantity; std::cin >> type >> id >> price >> quantity;

            exchange.AddOrder(Order{
                .orderId = id, 
                .arrivalTime = getTime(), 
                .price = price,
                .quantity = quantity,
                .side = type == "buy" ? Side::Buy : Side::Sell
            }); 
        } else if ( op == "rmv" ){
            int id; std::cin >> id;

            exchange.RemoveOrder(id);
        } else{
            int id; std::cin >> id;

            exchange.GetOrder(id).print();
        }
    }

    exchange.PrintLog();
}