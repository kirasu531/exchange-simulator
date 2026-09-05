#include <bits/stdc++.h>

struct Order{
    int OrderID, ArrivalTime, Price, Quantity; bool IsBuy;

    Order(int OrderID = -1, int ArrivalTime = 0, int Price = 0, int Quantity = 0, bool IsBuy = false) :
        OrderID(OrderID), ArrivalTime(ArrivalTime), Price(Price), Quantity(Quantity), IsBuy(IsBuy) {}

    bool operator <(const Order &t) const{
        assert(t.IsBuy == IsBuy);

        if ( t.IsBuy ){
            if ( t.Price == Price ){
                if ( t.ArrivalTime == ArrivalTime ){
                    return OrderID < t.OrderID;
                }

                return ArrivalTime < t.ArrivalTime;
            }
            
            return Price > t.Price;
        } else{
            if ( t.Price == Price ){
                if ( t.ArrivalTime == ArrivalTime ){
                    return OrderID < t.OrderID;
                }

                return ArrivalTime < t.ArrivalTime;
            }
            
            return Price < t.Price;
        }
    }

    void print(){
        std::cout << OrderID << ' ' << ArrivalTime << ' ' << Price << ' ' << Quantity << ' ' << IsBuy << '\n';
    }
};

struct OrderBook{
    std::set <Order> buy_list, sell_list;

    std::map <int, Order> all_orders, active_orders;

    std::vector <std::array<int, 3>> trades; // buyerID, sellerID, quantity

    OrderBook() {}

    void AddOrder(const Order &t){
        if ( all_orders.find(t.OrderID) != all_orders.end() ) return;

        all_orders[t.OrderID] = t;

        auto incoming = t;

        if ( incoming.IsBuy ){
            while ( sell_list.size() && incoming.Quantity > 0 && (*sell_list.begin()).Price <= incoming.Price ){
                auto [id, time, price, quantity, isbuy] = *sell_list.begin();

                sell_list.erase(sell_list.begin());
                active_orders.erase(id);

                trades.push_back({incoming.OrderID, id, std::min(incoming.Quantity, quantity)});

                if ( incoming.Quantity >= quantity ){
                    incoming.Quantity -= quantity;
                } else{
                    active_orders[id] = Order(id, time, price, quantity - incoming.Quantity, false);
                    sell_list.insert(active_orders[id]);

                    incoming.Quantity = 0;
                }
            }
        } else{
            while ( buy_list.size() && incoming.Quantity > 0 && (*buy_list.begin()).Price >= incoming.Price ){
                auto [id, time, price, quantity, isbuy] = *buy_list.begin();

                buy_list.erase(buy_list.begin());
                active_orders.erase(id);

                trades.push_back({id, incoming.OrderID, std::min(incoming.Quantity, quantity)});

                if ( incoming.Quantity >= quantity ){
                    incoming.Quantity -= quantity;
                } else{
                    active_orders[id] = Order(id, time, price, quantity - incoming.Quantity, true);
                    buy_list.insert(active_orders[id]);

                    incoming.Quantity = 0;
                }
            }
        }

        if ( incoming.Quantity ){
            if ( incoming.IsBuy ){
                buy_list.insert(incoming);
            } else{
                sell_list.insert(incoming); 
            }

            active_orders[incoming.OrderID] = incoming;
        }
    }

    void RemoveOrder(int ID){
        if ( active_orders.find(ID) != active_orders.end() ){
            auto cur = active_orders[ID];

            if ( cur.IsBuy ){
                buy_list.erase(cur);
            } else{
                sell_list.erase(cur);
            }

            active_orders.erase(ID);
        }
    }

    Order GetOrder(int ID){
        return active_orders.find(ID) != active_orders.end() ? active_orders[ID] : Order();
    }

    void PrintLog(){
        for ( auto &[buyerID, sellerID, quantity]: trades ){
            std::cout << buyerID << ' ' << sellerID << ' ' << quantity << '\n';
        }
    }
};