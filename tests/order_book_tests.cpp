#include <catch2/catch_test_macros.hpp>

#include "matching_engine.hpp"
#include "order_book.hpp"
#include "trade.hpp"

#include <optional>
#include <string>


Order getValidOrder(){
    return Order{
        .orderId = 0,
        .arrivalSequence = 0,
        .price = 100,
        .quantity = 1,
        .side = Side::Buy,
        .type = OrderType::Limit,
        .instrument = "BTC"
    };
}

Order getOrder(
    int orderId,
    int arrivalSequence,
    int price,
    int quantity,
    Side side,
    OrderType type = OrderType::Limit,
    std::string instrument = "BTC"
){
    return Order{
        .orderId = orderId,
        .arrivalSequence = arrivalSequence,
        .price = price,
        .quantity = quantity,
        .side = side,
        .type = type,
        .instrument = instrument
    };
}


TEST_CASE("Check GetOrder [OrderBook]"){
    OrderBook book; 
    Order A = getValidOrder();

    book.AddOrder(A);

    auto cur = book.GetOrder(0);

    REQUIRE(cur.has_value());
    REQUIRE(cur->orderId == A.orderId);
    REQUIRE(cur->price == A.price);
    REQUIRE(cur->quantity == A.quantity);

    REQUIRE(book.GetOrder(5) == std::nullopt);
}


TEST_CASE("Check RemoveOrder [OrderBook]"){
    OrderBook book; 
    Order A = getValidOrder();

    book.AddOrder(A);

    A.orderId = 5;
    A.arrivalSequence = 1;

    book.AddOrder(A);

    book.RemoveOrder(0);

    REQUIRE(book.GetOrder(0) == std::nullopt);
    REQUIRE(book.GetOrder(5).has_value());
}


TEST_CASE("Handle duplicate order Ids [OrderBook]"){
    OrderBook book; 
    Order A = getValidOrder();

    book.AddOrder(A);
    book.AddOrder(A);

    REQUIRE(book.GetOrder(0).has_value());

    A.side = Side::Sell;
    A.orderId = 1;
    A.arrivalSequence = 1;
    A.price = 105;

    book.AddOrder(A);
    book.AddOrder(A);

    REQUIRE(book.GetOrder(1).has_value());
}


TEST_CASE("Check Buy/Sell don't intersect [OrderBook]"){
    OrderBook book; 
    Order A = getValidOrder();

    book.AddOrder(A);

    A.side = Side::Sell;
    A.orderId = 1;
    A.arrivalSequence = 1;
    A.price = 105;

    book.AddOrder(A);

    REQUIRE(book.GetOrder(0).has_value());
    REQUIRE(book.GetOrder(1).has_value());
    REQUIRE(book.trades.empty());
}


TEST_CASE("Check IsValidOrder [Order]"){
    Order cur = getValidOrder(); 
    
    REQUIRE(IsValidOrder(cur));

    cur.orderId = -5; 
    REQUIRE(!IsValidOrder(cur));

    cur.orderId = 0;
    cur.arrivalSequence = -5; 
    REQUIRE(!IsValidOrder(cur));

    cur.arrivalSequence = 0;
    cur.instrument = ""; 
    REQUIRE(!IsValidOrder(cur));

    cur.instrument = "BTC";
    cur.quantity = 0;
    REQUIRE(!IsValidOrder(cur));

    cur.quantity = 1;
    cur.price = 0;
    REQUIRE(!IsValidOrder(cur));
}


TEST_CASE("Check comparator [Order]"){
    Order A, B; 
    A = B = getValidOrder();

    {
        ByIncreasingOrder comp;

        B.price = 101; 
        REQUIRE(comp(A, B));

        B.price = 100;
        B.arrivalSequence = 1; 
        REQUIRE(comp(A, B));

        B.arrivalSequence = 0;
        B.orderId = 1; 
        REQUIRE(comp(A, B));
    }

    {
        ByDecreasingOrder comp;

        B.price = 101; 
        REQUIRE(comp(B, A));

        B.price = 100;
        B.arrivalSequence = 1; 
        REQUIRE(comp(A, B));

        B.arrivalSequence = 0;
        B.orderId = 1; 
        REQUIRE(comp(A, B));
    }
}


TEST_CASE("Exact fill [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 100, 10, Side::Buy));
    book.AddOrder(getOrder(1, 1, 100, 10, Side::Sell));

    REQUIRE(book.GetOrder(0) == std::nullopt);
    REQUIRE(book.GetOrder(1) == std::nullopt);

    REQUIRE(book.trades.size() == 1);
    REQUIRE(book.trades[0].buyerId == 0);
    REQUIRE(book.trades[0].sellerId == 1);
    REQUIRE(book.trades[0].price == 100);
    REQUIRE(book.trades[0].quantity == 10);
}


TEST_CASE("Partial fill [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 100, 10, Side::Buy));
    book.AddOrder(getOrder(1, 1, 90, 4, Side::Sell));

    auto buyer = book.GetOrder(0);

    REQUIRE(buyer.has_value());
    REQUIRE(buyer->quantity == 6);

    REQUIRE(book.GetOrder(1) == std::nullopt);

    REQUIRE(book.trades.size() == 1);
    REQUIRE(book.trades[0].quantity == 4);
}


TEST_CASE("One order matches multiple orders [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 90, 3, Side::Sell));
    book.AddOrder(getOrder(1, 1, 95, 4, Side::Sell));

    book.AddOrder(getOrder(2, 2, 100, 6, Side::Buy));

    REQUIRE(book.GetOrder(0) == std::nullopt);

    auto second = book.GetOrder(1);

    REQUIRE(second.has_value());
    REQUIRE(second->quantity == 1);

    REQUIRE(book.GetOrder(2) == std::nullopt);

    REQUIRE(book.trades.size() == 2);

    REQUIRE(book.trades[0].sellerId == 0);
    REQUIRE(book.trades[0].quantity == 3);

    REQUIRE(book.trades[1].sellerId == 1);
    REQUIRE(book.trades[1].quantity == 3);
}


TEST_CASE("Best price priority [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 100, 5, Side::Sell));
    book.AddOrder(getOrder(1, 1, 90, 5, Side::Sell));

    book.AddOrder(getOrder(2, 2, 100, 5, Side::Buy));

    REQUIRE(book.trades.size() == 1);

    REQUIRE(book.trades[0].sellerId == 1);
    REQUIRE(book.trades[0].price == 90);

    REQUIRE(book.GetOrder(0).has_value());
    REQUIRE(book.GetOrder(1) == std::nullopt);
}


TEST_CASE("Arrival priority at same price [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 100, 5, Side::Sell));
    book.AddOrder(getOrder(1, 1, 100, 5, Side::Sell));

    book.AddOrder(getOrder(2, 2, 100, 5, Side::Buy));

    REQUIRE(book.trades.size() == 1);

    REQUIRE(book.trades[0].sellerId == 0);

    REQUIRE(book.GetOrder(0) == std::nullopt);
    REQUIRE(book.GetOrder(1).has_value());
}


TEST_CASE("Cancelled order doesn't trade [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 100, 5, Side::Sell));

    book.RemoveOrder(0);

    book.AddOrder(getOrder(1, 1, 100, 5, Side::Buy));

    REQUIRE(book.trades.empty());

    REQUIRE(book.GetOrder(0) == std::nullopt);
    REQUIRE(book.GetOrder(1).has_value());
}


TEST_CASE("Market buy consumes several sell orders [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 90, 3, Side::Sell));
    book.AddOrder(getOrder(1, 1, 100, 4, Side::Sell));

    book.AddOrder(
        getOrder(2, 2, 0, 5, Side::Buy, OrderType::Market)
    );

    REQUIRE(book.GetOrder(0) == std::nullopt);

    auto second = book.GetOrder(1);

    REQUIRE(second.has_value());
    REQUIRE(second->quantity == 2);

    REQUIRE(book.GetOrder(2) == std::nullopt);

    REQUIRE(book.trades.size() == 2);
    REQUIRE(book.trades[0].quantity == 3);
    REQUIRE(book.trades[1].quantity == 2);
}


TEST_CASE("Market sell consumes several buy orders [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 110, 3, Side::Buy));
    book.AddOrder(getOrder(1, 1, 100, 4, Side::Buy));

    book.AddOrder(
        getOrder(2, 2, 0, 5, Side::Sell, OrderType::Market)
    );

    REQUIRE(book.GetOrder(0) == std::nullopt);

    auto second = book.GetOrder(1);

    REQUIRE(second.has_value());
    REQUIRE(second->quantity == 2);

    REQUIRE(book.GetOrder(2) == std::nullopt);

    REQUIRE(book.trades.size() == 2);
}


TEST_CASE("Unfilled market order doesn't remain active [OrderBook]"){
    OrderBook book;

    book.AddOrder(
        getOrder(0, 0, 0, 10, Side::Buy, OrderType::Market)
    );

    REQUIRE(book.GetOrder(0) == std::nullopt);
    REQUIRE(book.trades.empty());
}


TEST_CASE("Different instruments don't interact [MatchingEngine]"){
    MatchingEngine engine;

    engine.AddOrder(
        getOrder(
            0, 0, 100, 5,
            Side::Buy,
            OrderType::Limit,
            "AAPL"
        )
    );

    engine.AddOrder(
        getOrder(
            1, 1, 90, 5,
            Side::Sell,
            OrderType::Limit,
            "NVDA"
        )
    );

    REQUIRE(engine.GetOrder(0).has_value());
    REQUIRE(engine.GetOrder(1).has_value());
}


TEST_CASE("Filled order returns nullopt [OrderBook]"){
    OrderBook book;

    book.AddOrder(getOrder(0, 0, 100, 5, Side::Buy));
    book.AddOrder(getOrder(1, 1, 100, 5, Side::Sell));

    REQUIRE(book.GetOrder(0) == std::nullopt);
    REQUIRE(book.GetOrder(1) == std::nullopt);
}