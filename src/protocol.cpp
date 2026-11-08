#include "protocol.hpp"

#include <charconv>

std::vector <std::string> tokenize(const std::string &s){
    std::vector <std::string> str;

    for ( size_t i = 0; i < s.size(); i++ ){
        if ( s[i] == ' ' ) continue;

        str.push_back("");

        size_t j = i;

        while ( j < s.size() && s[j] != ' ' ){
            str.back().push_back(s[j]);
            j += 1;
        }

        i = j;
    }

    return str;
}

bool ParseInt(const std::string &s, int &value){
    if ( s.empty() ) return false;

    auto [ptr, ec] = std::from_chars(
        s.data(),
        s.data() + s.size(),
        value
    );

    return ec == std::errc{} && ptr == s.data() + s.size();
}

ParsedCommand ParseCommand(const std::string &msg){
    ParsedCommand result;
    Order &order = result.order;

    std::vector <std::string> vec = tokenize(msg);

    if ( vec.empty() ) return result;

    if ( vec[0] == "ADD" ){
        if ( vec.size() != 7 ) return result;

        if (
            !ParseInt(vec[1], order.orderId) ||
            !ParseInt(vec[2], order.price) ||
            !ParseInt(vec[3], order.quantity) ||
            (vec[4] != "BUY" && vec[4] != "SELL") ||
            (vec[5] != "LIMIT" && vec[5] != "MARKET")
        ){

            return result;
        }


        order.side = vec[4] == "BUY" ? Side::Buy : Side::Sell;
        order.type = vec[5] == "LIMIT" ? OrderType::Limit : OrderType::Market;
        order.instrument = vec[6];

        if ( !IsValidOrder(order) ) return result;

        result.type = CommandType::Add;
        return result;
    }

    if ( vec[0] == "CANCEL" ){
        if ( vec.size() != 2 || !ParseInt(vec[1], order.orderId) || order.orderId < 0 ){
            return result;
        }

        result.type = CommandType::Cancel;
        return result;
    }

    if ( vec[0] == "GET" ){
        if ( vec.size() != 2 || !ParseInt(vec[1], order.orderId) || order.orderId < 0 ){
            return result;
        }

        result.type = CommandType::Get;
        return result;
    }

    if ( vec[0] == "TRADES" ){
        if ( vec.size() != 1 ) return result;

        result.type = CommandType::Trades;
        return result;
    }

    if ( vec[0] == "SUBSCRIBE" ){
        if ( vec.size() != 2 ) return result;

        order.instrument = vec[1];

        result.type = CommandType::Subscribe;
        return result;
    }

    if ( vec[0] == "UNSUBSCRIBE" ){
        if ( vec.size() != 2 ) return result;

        order.instrument = vec[1];

        result.type = CommandType::Unsubscribe;
        return result;
    }

    return result;
}

CommandResult HandleCommand(MatchingEngine &engine, const Order &order, const CommandType &type){
    if ( type == CommandType::Add ){
        size_t trades_before = engine.GetTradeCount(order.instrument);

        auto add_result = engine.AddOrder(order);

        CommandResult result;
        result.new_trades = engine.GetTradesFrom(order.instrument, trades_before);

        if ( add_result == AddOrderResult::Accepted ){
            result.reply = "Accepted";
            result.mutates = true;
        }
        else if ( add_result == AddOrderResult::DuplicateId ){
            result.reply = "Duplicate Id";
        }
        else{
            result.reply = "Invalid Query";
        }

        return result;
    }

    if ( type == CommandType::Cancel ){
        auto cancel_result = engine.CancelOrder(order.orderId);

        if ( cancel_result == CancelResult::Cancelled ){
            return {.reply = "Cancelled", .mutates = true};
        }

        return {.reply = "Not Found"};
    }

    if ( type == CommandType::Get ){
        auto result = engine.GetOrder(order.orderId);

        if ( result == std::nullopt ) return {.reply = "Not Found"};

        return {.reply =
            std::to_string(result -> orderId) + " " +
            std::to_string(result -> price) + " " +
            std::to_string(result -> quantity) + " " +
            (result -> side == Side::Buy ? "Buy" : "Sell") + " " +
            (result -> type == OrderType::Limit ? "Limit" : "Market") + " " +
            result -> instrument
        };
    }

    if ( type == CommandType::Trades ){
        return {.reply = std::to_string(engine.GetTrades().size())};
    }

    return {.reply = "Invalid Query"};
}
