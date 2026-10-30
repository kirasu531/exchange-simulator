// Matching-engine unit tests (Catch2).
//
// Exercises the public MatchingEngine API only: order validation,
// duplicate ids, arrival/trade sequencing, price-time priority,
// partial fills, market orders, lazy cancellation, and the trade log.
// Wire-protocol and socket behavior are covered separately by the
// Python suites in this directory.

#include <catch2/catch_test_macros.hpp>

#include "matching_engine.hpp"
#include "order_book.hpp"

#include <optional>
#include <string>


Order makeValidOrder(){
    return Order{
        .orderId = 0,
        .price = 100,
        .quantity = 1,
        .side = Side::Buy,
        .type = OrderType::Limit,
        .instrument = "BTC"
    };
}

Order makeOrder(
    int orderId,
    int price,
    int quantity,
    Side side,
    OrderType type = OrderType::Limit,
    std::string instrument = "BTC"
){
    return Order{
        .orderId = orderId,
        .price = price,
        .quantity = quantity,
        .side = side,
        .type = type,
        .instrument = instrument
    };
}


TEST_CASE("Check InvalidOrder verdict [MatchingEngine]"){
    MatchingEngine engine;

    Order cur = makeValidOrder();

    cur.orderId = -1;
    REQUIRE(engine.AddOrder(cur) == AddOrderResult::InvalidOrder);

    cur = makeValidOrder();
    cur.price = 0;
    REQUIRE(engine.AddOrder(cur) == AddOrderResult::InvalidOrder);

    cur = makeValidOrder();
    cur.price = -10;
    REQUIRE(engine.AddOrder(cur) == AddOrderResult::InvalidOrder);

    cur = makeValidOrder();
    cur.quantity = 0;
    REQUIRE(engine.AddOrder(cur) == AddOrderResult::InvalidOrder);

    cur = makeValidOrder();
    cur.quantity = -10;
    REQUIRE(engine.AddOrder(cur) == AddOrderResult::InvalidOrder);

    cur = makeValidOrder();
    cur.instrument = "";
    REQUIRE(engine.AddOrder(cur) == AddOrderResult::InvalidOrder);
}


TEST_CASE("Check Accepted verdict [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 1, Side::Buy);
    Order B = makeOrder(1, 90, 1, Side::Buy);
    Order C = makeOrder(2, 200, 1, Side::Buy, OrderType::Limit, "AAPL");

    REQUIRE(engine.AddOrder(A) == AddOrderResult::Accepted);
    REQUIRE(engine.AddOrder(B) == AddOrderResult::Accepted);
    REQUIRE(engine.AddOrder(C) == AddOrderResult::Accepted);
}


TEST_CASE("Check DuplicateId verdict [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 5, Side::Buy, OrderType::Limit, "BTC");

    REQUIRE(engine.AddOrder(A) == AddOrderResult::Accepted);

    Order B = makeOrder(0, 500, 20, Side::Sell, OrderType::Limit, "BTC");

    REQUIRE(engine.AddOrder(B) == AddOrderResult::DuplicateId);

    Order C = A;
    C.instrument = "AAPL";

    REQUIRE(engine.AddOrder(C) == AddOrderResult::DuplicateId);
}


TEST_CASE("Check GetOrder and arrival sequence [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 1, Side::Buy);

    REQUIRE(engine.AddOrder(A) == AddOrderResult::Accepted);

    auto first = engine.GetOrder(A.orderId);

    REQUIRE(first.has_value());
    REQUIRE(first->arrivalSequence == 0);

    Order invalid = makeOrder(1, 0, 1, Side::Buy);

    REQUIRE(engine.AddOrder(invalid) == AddOrderResult::InvalidOrder);

    Order B = makeOrder(2, 90, 1, Side::Buy);

    REQUIRE(engine.AddOrder(B) == AddOrderResult::Accepted);

    auto second = engine.GetOrder(B.orderId);

    REQUIRE(second.has_value());
    REQUIRE(second->arrivalSequence == 1);
}


TEST_CASE("Non-crossing buy and sell remain active [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 90, 5, Side::Buy);
    Order B = makeOrder(1, 100, 5, Side::Sell);

    engine.AddOrder(A);
    engine.AddOrder(B);

    REQUIRE(engine.GetOrder(A.orderId).has_value());
    REQUIRE(engine.GetOrder(B.orderId).has_value());
}


TEST_CASE("Different instruments don't cross [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(
        0, 200, 5,
        Side::Buy,
        OrderType::Limit,
        "BTC"
    );

    Order B = makeOrder(
        1, 100, 5,
        Side::Sell,
        OrderType::Limit,
        "AAPL"
    );

    engine.AddOrder(A);
    engine.AddOrder(B);

    REQUIRE(engine.GetOrder(A.orderId).has_value());
    REQUIRE(engine.GetOrder(B.orderId).has_value());
}


TEST_CASE("Partial fill leaves correct remaining quantity [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 3, Side::Buy);
    Order B = makeOrder(1, 90, 7, Side::Sell);

    engine.AddOrder(A);
    engine.AddOrder(B);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());

    auto remaining = engine.GetOrder(B.orderId);

    REQUIRE(remaining.has_value());
    REQUIRE(remaining->quantity == 4);
}


TEST_CASE("Best bid is matched first [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 1, Side::Buy);
    Order B = makeOrder(1, 90, 1, Side::Buy);
    Order C = makeOrder(2, 100, 1, Side::Sell);

    engine.AddOrder(B);
    engine.AddOrder(A);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(engine.GetOrder(B.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());
}


TEST_CASE("Best ask is matched first [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 90, 1, Side::Sell);
    Order B = makeOrder(1, 100, 1, Side::Sell);
    Order C = makeOrder(2, 100, 1, Side::Buy);

    engine.AddOrder(B);
    engine.AddOrder(A);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(engine.GetOrder(B.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());
}


TEST_CASE("Sell order matches several buy orders [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 110, 2, Side::Buy);
    Order B = makeOrder(1, 100, 5, Side::Buy);
    Order C = makeOrder(2, 90, 4, Side::Sell);

    engine.AddOrder(B);
    engine.AddOrder(A);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());

    auto remaining = engine.GetOrder(B.orderId);

    REQUIRE(remaining.has_value());
    REQUIRE(remaining->quantity == 3);
}


TEST_CASE("Buy order matches several sell orders [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 90, 2, Side::Sell);
    Order B = makeOrder(1, 100, 5, Side::Sell);
    Order C = makeOrder(2, 110, 4, Side::Buy);

    engine.AddOrder(B);
    engine.AddOrder(A);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());

    auto remaining = engine.GetOrder(B.orderId);

    REQUIRE(remaining.has_value());
    REQUIRE(remaining->quantity == 3);
}


TEST_CASE("FIFO priority for sells at same price [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 1, Side::Sell);
    Order B = makeOrder(1, 100, 1, Side::Sell);
    Order C = makeOrder(2, 100, 1, Side::Buy);

    engine.AddOrder(A);
    engine.AddOrder(B);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(engine.GetOrder(B.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());
}


TEST_CASE("FIFO priority for buys at same price [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 1, Side::Buy);
    Order B = makeOrder(1, 100, 1, Side::Buy);
    Order C = makeOrder(2, 100, 1, Side::Sell);

    engine.AddOrder(A);
    engine.AddOrder(B);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(engine.GetOrder(B.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());
}

TEST_CASE("Market order doesn't rest [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 0, 5, Side::Buy, OrderType::Market, "BTC");

    REQUIRE(engine.AddOrder(A) == AddOrderResult::Accepted);
    REQUIRE(!engine.GetOrder(A.orderId).has_value());
}

TEST_CASE("Cancel active order [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 5, Side::Buy, OrderType::Limit, "BTC");

    REQUIRE(engine.AddOrder(A) == AddOrderResult::Accepted);

    REQUIRE(engine.CancelOrder(A.orderId) == CancelResult::Cancelled);
    REQUIRE(!engine.GetOrder(A.orderId).has_value());

    REQUIRE(engine.CancelOrder(A.orderId) == CancelResult::NotFound);
}


TEST_CASE("Cancel fully filled orders [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 5, Side::Buy, OrderType::Limit, "BTC");
    Order B = makeOrder(1, 100, 5, Side::Sell, OrderType::Limit, "BTC");

    engine.AddOrder(A);
    engine.AddOrder(B);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(!engine.GetOrder(B.orderId).has_value());

    REQUIRE(engine.CancelOrder(A.orderId) == CancelResult::NotFound);
    REQUIRE(engine.CancelOrder(B.orderId) == CancelResult::NotFound);
}


TEST_CASE("Check trade log for one instrument [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 110, 2, Side::Buy, OrderType::Limit, "BTC");
    Order B = makeOrder(1, 100, 3, Side::Buy, OrderType::Limit, "BTC");
    Order C = makeOrder(2, 90, 5, Side::Sell, OrderType::Limit, "BTC");

    engine.AddOrder(A);
    engine.AddOrder(B);
    engine.AddOrder(C);

    auto trades = engine.GetTrades();

    REQUIRE(trades.size() == 2);

    REQUIRE(trades[0].tradeSequence == 0);
    REQUIRE(trades[0].buyerId == A.orderId);
    REQUIRE(trades[0].sellerId == C.orderId);
    REQUIRE(trades[0].instrument == "BTC");
    REQUIRE(trades[0].price == A.price);
    REQUIRE(trades[0].quantity == A.quantity);

    REQUIRE(trades[1].tradeSequence == 1);
    REQUIRE(trades[1].buyerId == B.orderId);
    REQUIRE(trades[1].sellerId == C.orderId);
    REQUIRE(trades[1].instrument == "BTC");
    REQUIRE(trades[1].price == B.price);
    REQUIRE(trades[1].quantity == B.quantity);
}


TEST_CASE("Trade log is global across instruments [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 110, 1, Side::Buy, OrderType::Limit, "BTC");
    Order B = makeOrder(1, 100, 1, Side::Sell, OrderType::Limit, "BTC");
    Order C = makeOrder(2, 210, 1, Side::Buy, OrderType::Limit, "AAPL");
    Order D = makeOrder(3, 200, 1, Side::Sell, OrderType::Limit, "AAPL");

    engine.AddOrder(A);
    engine.AddOrder(C);
    engine.AddOrder(B);
    engine.AddOrder(D);

    auto trades = engine.GetTrades();

    REQUIRE(trades.size() == 2);

    REQUIRE(trades[0].tradeSequence == 0);
    REQUIRE(trades[0].buyerId == A.orderId);
    REQUIRE(trades[0].sellerId == B.orderId);
    REQUIRE(trades[0].instrument == "BTC");
    REQUIRE(trades[0].price == A.price);
    REQUIRE(trades[0].quantity == 1);

    REQUIRE(trades[1].tradeSequence == 1);
    REQUIRE(trades[1].buyerId == C.orderId);
    REQUIRE(trades[1].sellerId == D.orderId);
    REQUIRE(trades[1].instrument == "AAPL");
    REQUIRE(trades[1].price == C.price);
    REQUIRE(trades[1].quantity == 1);
}

TEST_CASE("Market buy consumes available sell orders [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 90, 2, Side::Sell);
    Order B = makeOrder(1, 100, 3, Side::Sell);
    Order C = makeOrder(2, 0, 4, Side::Buy, OrderType::Market);

    engine.AddOrder(B);
    engine.AddOrder(A);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());

    auto remaining = engine.GetOrder(B.orderId);

    REQUIRE(remaining.has_value());
    REQUIRE(remaining->quantity == 1);

    auto trades = engine.GetTrades();

    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].sellerId == A.orderId);
    REQUIRE(trades[0].price == A.price);
    REQUIRE(trades[0].quantity == 2);

    REQUIRE(trades[1].sellerId == B.orderId);
    REQUIRE(trades[1].price == B.price);
    REQUIRE(trades[1].quantity == 2);
}


TEST_CASE("Market sell consumes available buy orders [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 110, 2, Side::Buy);
    Order B = makeOrder(1, 100, 3, Side::Buy);
    Order C = makeOrder(2, 0, 4, Side::Sell, OrderType::Market);

    engine.AddOrder(B);
    engine.AddOrder(A);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());

    auto remaining = engine.GetOrder(B.orderId);

    REQUIRE(remaining.has_value());
    REQUIRE(remaining->quantity == 1);

    auto trades = engine.GetTrades();

    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].buyerId == A.orderId);
    REQUIRE(trades[0].price == A.price);
    REQUIRE(trades[0].quantity == 2);

    REQUIRE(trades[1].buyerId == B.orderId);
    REQUIRE(trades[1].price == B.price);
    REQUIRE(trades[1].quantity == 2);
}


TEST_CASE("Market order larger than entire opposite book, doesn't rest [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 90, 2, Side::Sell);
    Order B = makeOrder(1, 100, 3, Side::Sell);
    Order C = makeOrder(2, 0, 10, Side::Buy, OrderType::Market);

    engine.AddOrder(A);
    engine.AddOrder(B);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(!engine.GetOrder(B.orderId).has_value());
    REQUIRE(!engine.GetOrder(C.orderId).has_value());

    auto trades = engine.GetTrades();

    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].quantity == 2);
    REQUIRE(trades[1].quantity == 3);
}


TEST_CASE("Limit order larger than entire opposite book, rests with remainder [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 90, 2, Side::Sell);
    Order B = makeOrder(1, 100, 3, Side::Sell);
    Order C = makeOrder(2, 110, 8, Side::Buy);

    engine.AddOrder(A);
    engine.AddOrder(B);
    engine.AddOrder(C);

    REQUIRE(!engine.GetOrder(A.orderId).has_value());
    REQUIRE(!engine.GetOrder(B.orderId).has_value());

    auto remaining = engine.GetOrder(C.orderId);

    REQUIRE(remaining.has_value());
    REQUIRE(remaining->quantity == 3);

    auto trades = engine.GetTrades();

    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].quantity == 2);
    REQUIRE(trades[1].quantity == 3);
}


TEST_CASE("Cancelled resting order is skipped during matching [MatchingEngine]"){
    MatchingEngine engine;

    // Lazy cancellation: the order stays in its price-level queue until
    // a crossing order pops it; the skip must produce no trade.
    Order A = makeOrder(0, 100, 5, Side::Buy);
    Order B = makeOrder(1, 100, 3, Side::Sell);

    engine.AddOrder(A);

    REQUIRE(engine.CancelOrder(A.orderId) == CancelResult::Cancelled);

    engine.AddOrder(B);

    REQUIRE(engine.GetTrades().empty());

    auto resting = engine.GetOrder(B.orderId);

    REQUIRE(resting.has_value());
    REQUIRE(resting->quantity == 3);
}


TEST_CASE("Cancelled order at a shared price level is skipped [MatchingEngine]"){
    MatchingEngine engine;

    // A and B sit in the same FIFO queue; A is cancelled, so a crossing
    // sell must fill only B.
    Order A = makeOrder(0, 100, 1, Side::Buy);
    Order B = makeOrder(1, 100, 2, Side::Buy);
    Order C = makeOrder(2, 100, 3, Side::Sell);

    engine.AddOrder(A);
    engine.AddOrder(B);
    engine.CancelOrder(A.orderId);
    engine.AddOrder(C);

    auto trades = engine.GetTrades();

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].buyerId == B.orderId);
    REQUIRE(trades[0].quantity == 2);

    auto resting = engine.GetOrder(C.orderId);

    REQUIRE(resting.has_value());
    REQUIRE(resting->quantity == 1);
}


TEST_CASE("Cancel a partially filled order [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 10, Side::Buy);
    Order B = makeOrder(1, 100, 4, Side::Sell);

    engine.AddOrder(A);
    engine.AddOrder(B);

    auto remaining = engine.GetOrder(A.orderId);

    REQUIRE(remaining.has_value());
    REQUIRE(remaining->quantity == 6);

    REQUIRE(engine.CancelOrder(A.orderId) == CancelResult::Cancelled);
    REQUIRE(!engine.GetOrder(A.orderId).has_value());
}


TEST_CASE("Order id is never reusable [MatchingEngine]"){
    MatchingEngine engine;

    // The engine keeps a permanent id -> instrument record, so an id
    // stays taken after its order is cancelled...
    Order A = makeOrder(0, 100, 5, Side::Buy);

    engine.AddOrder(A);
    engine.CancelOrder(A.orderId);

    REQUIRE(engine.AddOrder(makeOrder(0, 50, 1, Side::Sell)) == AddOrderResult::DuplicateId);

    // ...and also after it is fully filled.
    Order B = makeOrder(1, 100, 5, Side::Buy);
    Order C = makeOrder(2, 100, 5, Side::Sell);

    engine.AddOrder(B);
    engine.AddOrder(C);

    REQUIRE(engine.AddOrder(makeOrder(1, 90, 1, Side::Buy)) == AddOrderResult::DuplicateId);
    REQUIRE(engine.AddOrder(makeOrder(2, 90, 1, Side::Buy)) == AddOrderResult::DuplicateId);
}


TEST_CASE("Rejected adds do not consume arrival sequence [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 1, Side::Buy);

    engine.AddOrder(A);

    // A duplicate-id sell would cross A if it were actually accepted;
    // the rejected attempt must leave both the book and the sequence
    // untouched.
    engine.AddOrder(makeOrder(0, 90, 9, Side::Sell));

    Order B = makeOrder(1, 90, 1, Side::Buy);

    engine.AddOrder(B);

    auto second = engine.GetOrder(B.orderId);

    REQUIRE(second.has_value());
    REQUIRE(second->arrivalSequence == 1);
    REQUIRE(engine.GetTrades().empty());
}


TEST_CASE("Price level is recreated after being fully consumed [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 1, Side::Sell);
    Order B = makeOrder(1, 100, 1, Side::Buy);

    engine.AddOrder(A);
    engine.AddOrder(B);  // fills A and erases the level

    Order C = makeOrder(2, 100, 3, Side::Sell);

    engine.AddOrder(C);

    auto resting = engine.GetOrder(C.orderId);

    REQUIRE(resting.has_value());
    REQUIRE(resting->quantity == 3);
}


TEST_CASE("Market order against a fully cancelled level trades nothing [MatchingEngine]"){
    MatchingEngine engine;

    Order A = makeOrder(0, 100, 5, Side::Sell);

    engine.AddOrder(A);
    engine.CancelOrder(A.orderId);

    Order B = makeOrder(1, 0, 5, Side::Buy, OrderType::Market);

    engine.AddOrder(B);

    REQUIRE(engine.GetTrades().empty());
    REQUIRE(!engine.GetOrder(B.orderId).has_value());
}