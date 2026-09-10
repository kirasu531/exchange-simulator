#include "matching_engine.hpp"

#include <vector>
#include <algorithm>
#include <optional>

AddOrderResult MatchingEngine::AddOrder(const Order &newOrder){
    if ( used_Ids.find(newOrder.orderId) != used_Ids.end() ) return AddOrderResult::DuplicateId;

    if ( !IsValidOrder(newOrder) ) return AddOrderResult::InvalidOrder;
    
    auto incoming = newOrder;

    incoming.arrivalSequence = nextSequence++;
    
    type[incoming.orderId] = incoming.instrument;
    used_Ids.insert(incoming.orderId);

    if ( groups.find(incoming.instrument) == groups.end() ){
        groups[incoming.instrument] = OrderBook();
    }

    groups[incoming.instrument].AddOrder(incoming, nextTradeNumber);

    return AddOrderResult::Accepted;
}

CancelResult MatchingEngine::CancelOrder(int Id){
    if ( type.find(Id) == type.end() ) return CancelResult::NotFound;

    return groups[type[Id]].RemoveOrder(Id);
}

std:: optional<Order> MatchingEngine::GetOrder(int Id){
    if ( type.find(Id) == type.end() ) return std::nullopt;

    return groups[type[Id]].GetOrder(Id);
}

std::vector <Trade> MatchingEngine::GetTrades(){
    std::vector <Trade> trade_log;
    
    for ( auto &[instrument, OrderBook]: groups ){
        for ( auto &trades: OrderBook.trades ){
            trade_log.push_back(trades);
        }
    }

    sort(begin(trade_log), end(trade_log), [&](const Trade &tradeA, const Trade &tradeB){
        return tradeA.tradeSequence < tradeB.tradeSequence;
    });
    
    return trade_log;
}

void MatchingEngine::PrintTrades(){
    auto trade_log = GetTrades();

    for ( auto &trade: trade_log ){
        trade.printTrade();
    }
}