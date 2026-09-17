#include "order_book.hpp"


void Order::print(){
    std::cout << orderId << ' ' 
    << arrivalSequence << ' ' 
    << price << ' ' 
    << quantity << ' ' 
    << (side == Side::Buy ? "Buy" : "Sell") << ' ' 
    << instrument << '\n';
}

bool IsValidOrder(const Order &order){
    if ( order.orderId < 0 ) return false;
    if ( order.type == OrderType::Limit && order.price <= 0 ) return false;
    if ( order.quantity <= 0 ) return false;
    if ( order.instrument.empty() ) return false;

    return true;
}

bool operator ==(const Order &First, const Order &Second){
    return First.orderId == Second.orderId && First.arrivalSequence == Second.arrivalSequence && 
        First.price == Second.price && First.quantity == Second.quantity && First.side == Second.side &&
            First.type == Second.type && First.instrument == Second.instrument;
}

bool ByIncreasingOrder::operator ()(const Order &First, const Order &Second) const{
    if ( First.price == Second.price ){
        if ( First.arrivalSequence == Second.arrivalSequence ){
            return First.orderId < Second.orderId;
        }

        return First.arrivalSequence < Second.arrivalSequence;
    }
    
    return First.price < Second.price;
}

bool ByDecreasingOrder::operator ()(const Order &First, const Order &Second) const{
    if ( First.price == Second.price ){
        if ( First.arrivalSequence == Second.arrivalSequence ){
            return First.orderId < Second.orderId;
        }

        return First.arrivalSequence < Second.arrivalSequence;
    }
    
    return First.price > Second.price;
}

AddOrderResult OrderBook::AddOrder(const Order &newOrder, int &nextTradeNumber){
    auto incoming = newOrder;

    if ( incoming.side == Side::Buy ){
        while ( !sell_list.empty() && incoming.quantity > 0 && 
            (sell_list.begin() -> first <= incoming.price || incoming.type == OrderType::Market ) )
        {
            auto &Queue = sell_list.begin() -> second;

            while ( !Queue.empty() && incoming.quantity > 0 ){
                auto &cur = Queue.front();

                if ( canceled_orders.find(cur.orderId) != canceled_orders.end() ){
                    Queue.pop();

                    continue;
                }

                trades.push_back(Trade{
                    .tradeSequence = nextTradeNumber++,
                    .buyerId = incoming.orderId,
                    .sellerId = cur.orderId,
                    .instrument = cur.instrument,
                    .price = cur.price,
                    .quantity = std::min(incoming.quantity, cur.quantity)
                }); 

                if ( incoming.quantity >= cur.quantity ){
                    active_orders.erase(cur.orderId);
                    incoming.quantity -= cur.quantity;

                    Queue.pop();
                } else{
                    active_orders[cur.orderId].quantity -= incoming.quantity;
                    cur.quantity -= incoming.quantity;
                    incoming.quantity = 0;
                }
            }

            if ( Queue.empty() ){
                sell_list.erase(sell_list.begin());
            }
        }
    } else{
        while ( !buy_list.empty() && incoming.quantity > 0 
                && (buy_list.rbegin() -> first >= incoming.price || incoming.type == OrderType::Market ) )
        {
            auto &Queue = buy_list.rbegin() -> second;

            while ( !Queue.empty() && incoming.quantity > 0 ){
                auto &cur = Queue.front();

                if ( canceled_orders.find(cur.orderId) != canceled_orders.end() ){
                    Queue.pop();

                    continue;
                }

                trades.push_back(Trade{
                    .tradeSequence = nextTradeNumber++,
                    .buyerId = cur.orderId,
                    .sellerId = incoming.orderId,
                    .instrument = cur.instrument,
                    .price = cur.price,
                    .quantity = std::min(incoming.quantity, cur.quantity)
                }); 

                if ( incoming.quantity >= cur.quantity ){
                    active_orders.erase(cur.orderId);
                    incoming.quantity -= cur.quantity;

                    Queue.pop();
                } else{
                    active_orders[cur.orderId].quantity -= incoming.quantity;
                    cur.quantity -= incoming.quantity;
                    incoming.quantity = 0;
                }
            }

            if ( Queue.empty() ){
                buy_list.erase(--buy_list.end());
            }
        }
    }

    if ( incoming.quantity && incoming.type != OrderType::Market ){
        if ( incoming.side == Side::Buy ){
            buy_list[incoming.price].push(incoming);
        } else{
            sell_list[incoming.price].push(incoming);
        }

        active_orders[incoming.orderId] = incoming;
    }

    return AddOrderResult::Accepted;
}

CancelResult OrderBook::RemoveOrder(int Id){
    auto iter = active_orders.find(Id);
    
    if ( iter != active_orders.end() ){
        canceled_orders.insert(Id);

        active_orders.erase(iter);

        return CancelResult::Cancelled;
    } else{
        return CancelResult::NotFound;
    }
}

std:: optional <Order> OrderBook::GetOrder(int Id){
    auto iter = active_orders.find(Id);

    if ( iter != active_orders.end() ){
        return iter -> second;
    }
        
    return std::nullopt;
}

std::vector <Trade> OrderBook::GetTrades(){ return trades; } 

void OrderBook::PrintLog(){
    for ( auto &[tradeSequence, buyerId, sellerId, instrument, price, quantity]: trades ){
        std::cout << tradeSequence << ' '  
        << buyerId << ' ' 
        << sellerId << ' ' 
        << instrument << ' ' 
        << price << ' ' 
        << quantity << '\n';
    }
}