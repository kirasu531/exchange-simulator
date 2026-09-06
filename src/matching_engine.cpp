#include "matching_engine.hpp"

#include <vector>
#include <algorithm>
#include <optional>

void MatchingEngine::AddOrder(const Order &newOrder){
    if ( used_Ids.find(newOrder.orderId) != used_Ids.end() ) return;
    
    type[newOrder.orderId] = newOrder.instrument;
    used_Ids.insert(newOrder.orderId);

    if ( groups.find(newOrder.instrument) == groups.end() ){
        groups[newOrder.instrument] = OrderBook();
    }

    groups[newOrder.instrument].AddOrder(newOrder);
}

void MatchingEngine::CancelOrder(int Id){
    if ( type.find(Id) == type.end() ) return;

    groups[type[Id]].RemoveOrder(Id);
}

std:: optional<Order> MatchingEngine::GetOrder(int Id){
    if ( type.find(Id) == type.end() ) return std::nullopt;

    return groups[type[Id]].GetOrder(Id);
}

void MatchingEngine::PrintTrades(){
    std::vector <Trade> trade_log;
    
    for ( auto &[instrument, OrderBook]: groups ){
        for ( auto &trades: OrderBook.trades ){
            trade_log.push_back(trades);
        }
    }

    sort(begin(trade_log), end(trade_log), [&](const Trade &tradeA, const Trade &tradeB){
        return tradeA.tradeTime < tradeB.tradeTime;
    });

    for ( auto &trade: trade_log ){
        trade.printTrade();
    }
}