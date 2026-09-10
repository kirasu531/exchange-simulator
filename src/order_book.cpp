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
    if ( all_orders.find(newOrder.orderId) != all_orders.end() ) return AddOrderResult::DuplicateId;

    all_orders[newOrder.orderId] = newOrder;

    auto incoming = newOrder;

    if ( incoming.side == Side::Buy ){
        while ( !sell_list.empty() && incoming.quantity > 0 && 
            ((*sell_list.begin()).price <= incoming.price || incoming.type == OrderType::Market ) )
        {
            auto cur = *sell_list.begin();

            sell_list.erase(sell_list.begin());
            active_orders.erase(cur.orderId);

            trades.push_back(Trade{
                .tradeSequence = nextTradeNumber++,
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
                    .arrivalSequence = cur.arrivalSequence,
                    .price = cur.price,
                    .quantity = cur.quantity - incoming.quantity,
                    .side = cur.side,
                    .type = OrderType::Limit,
                    .instrument = cur.instrument
                };

                sell_list.insert(active_orders[cur.orderId]);

                incoming.quantity = 0;
            }
        }
    } else{
        while ( !buy_list.empty() && incoming.quantity > 0 
                && ((*buy_list.begin()).price >= incoming.price || incoming.type == OrderType::Market ) )
        {
            auto cur = *buy_list.begin();

            buy_list.erase(buy_list.begin());
            active_orders.erase(cur.orderId);

            trades.push_back(Trade{
                .tradeSequence = nextTradeNumber++,
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
                    .arrivalSequence = cur.arrivalSequence,
                    .price = cur.price,
                    .quantity = cur.quantity - incoming.quantity,
                    .side = cur.side,
                    .type = OrderType::Limit,
                    .instrument = cur.instrument
                };
                
                buy_list.insert(active_orders[cur.orderId]);

                incoming.quantity = 0;
            }
        }
    }

    if ( incoming.quantity && incoming.type != OrderType::Market ){
        if ( incoming.side == Side::Buy ){
            buy_list.insert(incoming);
        } else{
            sell_list.insert(incoming); 
        }

        active_orders[incoming.orderId] = incoming;
    }

    return AddOrderResult::Accepted;
}

CancelResult OrderBook::RemoveOrder(int Id){
    auto iter = active_orders.find(Id);
    
    if ( iter != active_orders.end() ){
        auto cur = iter -> second;

        if ( cur.side == Side::Buy ){
            buy_list.erase(cur);
        } else{
            sell_list.erase(cur);
        }

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