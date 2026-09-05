#include <bits/stdc++.h>

#include "order_book.hpp"

using namespace std;

int cntTime = 0;

int getTime(){ return cntTime++; }

signed main(){
    int t; cin >> t;

    OrderBook exchange;

    while ( t-- ){
        string op; cin >> op;

        if ( op == "add" ){
            string type; int id, price, quantity; cin >> type >> id >> price >> quantity;

            exchange.AddOrder(Order(id, getTime(), price, quantity, type == "buy")); 
        } else if ( op == "rmv" ){
            int id; cin >> id;

            exchange.RemoveOrder(id);
        } else{
            int id; cin >> id;

            exchange.GetOrder(id).print();
        }
    }

    exchange.PrintLog();
}