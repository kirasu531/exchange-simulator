#include "order_book.hpp"


void Order::print(){
    std::cout << orderId << ' ' 
    << arrivalTime << ' ' 
    << price << ' ' 
    << quantity << ' ' 
    << (side == Side::Buy ? "Buy" : "Sell") << ' ' 
    << instrument << '\n';
}

bool operator <(const Order &First, const Order &Second){
    assert(First.side == Second.side);

    if ( First.side == Side::Buy ){
        if ( First.price == Second.price ){
            if ( First.arrivalTime == Second.arrivalTime ){
                return First.orderId < Second.orderId;
            }

            return First.arrivalTime < Second.arrivalTime;
        }
        
        return First.price > Second.price;
    } else{
        if ( First.price == Second.price ){
            if ( First.arrivalTime == Second.arrivalTime ){
                return First.orderId < Second.orderId;
            }

            return First.arrivalTime < Second.arrivalTime;
        }
        
        return First.price < Second.price;
    }
}

void OrderBook::AddOrder(const Order &newOrder){
    if ( all_orders.find(newOrder.orderId) != all_orders.end() ) return;

    all_orders[newOrder.orderId] = newOrder;

    auto incoming = newOrder;

    if ( incoming.side == Side::Buy ){
        while ( !sell_list.empty() && incoming.quantity > 0 && (*sell_list.begin()).price <= incoming.price ){
            auto cur = *sell_list.begin();

            sell_list.erase(sell_list.begin());
            active_orders.erase(cur.orderId);

            trades.push_back(Trade{
                .tradeTime = incoming.arrivalTime,
                .buyerId = incoming.orderId,
                .sellerId = cur.orderId,
                .instrument = cur.instrument,
                .price = cur.price,
                .quantity = std::min(incoming.quantity, cur.quantity)
            }); 

            if ( incoming.quantity >= cur.quantity ){
                incoming.quantity -= cur.quantity;
            } else{
                active_orders[cur.orderId] = Order{
                    .orderId = cur.orderId,
                    .arrivalTime = cur.arrivalTime,
                    .price = cur.price,
                    .quantity = cur.quantity - incoming.quantity,
                    .side = cur.side,
                    .instrument = cur.instrument
                };

                sell_list.insert(active_orders[cur.orderId]);

                incoming.quantity = 0;
            }
        }
    } else{
        while ( !buy_list.empty() && incoming.quantity > 0 && (*buy_list.begin()).price >= incoming.price ){
            auto cur = *buy_list.begin();

            buy_list.erase(buy_list.begin());
            active_orders.erase(cur.orderId);

            trades.push_back(Trade{
                .tradeTime = incoming.arrivalTime,
                .buyerId = cur.orderId,
                .sellerId = incoming.orderId,
                .instrument = cur.instrument,
                .price = cur.price,
                .quantity = std::min(incoming.quantity, cur.quantity)
            }); 

            if ( incoming.quantity >= cur.quantity ){
                incoming.quantity -= cur.quantity;
            } else{
                active_orders[cur.orderId] = Order{
                    .orderId = cur.orderId,
                    .arrivalTime = cur.arrivalTime,
                    .price = cur.price,
                    .quantity = cur.quantity - incoming.quantity,
                    .side = cur.side,
                    .instrument = cur.instrument
                };
                
                buy_list.insert(active_orders[cur.orderId]);

                incoming.quantity = 0;
            }
        }
    }

    if ( incoming.quantity ){
        if ( incoming.side == Side::Buy ){
            buy_list.insert(incoming);
        } else{
            sell_list.insert(incoming); 
        }

        active_orders[incoming.orderId] = incoming;
    }
}

void OrderBook::RemoveOrder(int Id){
    auto iter = active_orders.find(Id);
    
    if ( iter != active_orders.end() ){
        auto cur = iter -> second;

        if ( cur.side == Side::Buy ){
            buy_list.erase(cur);
        } else{
            sell_list.erase(cur);
        }

        active_orders.erase(iter);
    }
}

std:: optional <Order> OrderBook::GetOrder(int Id){
    auto iter = active_orders.find(Id);

    if ( iter != active_orders.end() ){
        return iter -> second;
    }
        
    return std::nullopt;
}

void OrderBook::PrintLog(){
    for ( auto &[tradeTime, buyerId, sellerId, instrument, price, quantity]: trades ){
        std::cout << tradeTime << ' '  
        << buyerId << ' ' 
        << sellerId << ' ' 
        << instrument << ' ' 
        << price << ' ' 
        << quantity << '\n';
    }
}