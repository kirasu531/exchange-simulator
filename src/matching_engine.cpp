#include "matching_engine.hpp"

#include <vector>
#include <algorithm>
#include <optional>

AddOrderResult MatchingEngine::AddOrder(const Order &newOrder){
    if ( order_instruments.find(newOrder.orderId) != order_instruments.end() ) return AddOrderResult::DuplicateId;

    if ( !IsValidOrder(newOrder) ) return AddOrderResult::InvalidOrder;
    
    auto incoming = newOrder;

    incoming.arrivalSequence = nextSequence++;

    order_instruments[incoming.orderId] = incoming.instrument;

    return books[incoming.instrument].AddOrder(incoming, nextTradeNumber);
}

CancelResult MatchingEngine::CancelOrder(int orderId){
    if ( order_instruments.find(orderId) == order_instruments.end() ) return CancelResult::NotFound;

    return books[order_instruments[orderId]].CancelOrder(orderId);
}

std:: optional<Order> MatchingEngine::GetOrder(int orderId){
    if ( order_instruments.find(orderId) == order_instruments.end() ) return std::nullopt;

    return books[order_instruments[orderId]].GetOrder(orderId);
}

std::vector <Trade> MatchingEngine::GetTrades(){
    std::vector <Trade> trade_log;
    
    for ( auto &[instrument, book]: books ){
        for ( auto trades: book.GetTrades() ){
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

size_t MatchingEngine::GetTradeCount(const std::string &instrument){
    auto it = books.find(instrument);

    if ( it == books.end() ) return {};

    return it -> second.GetTradeCount();
}

std::vector <Trade> MatchingEngine::GetTradesFrom(const std::string &instrument, size_t start){
    auto it = books.find(instrument);

    if ( it == books.end() ) return {};

    return it -> second.GetTradesFrom(start);
}

