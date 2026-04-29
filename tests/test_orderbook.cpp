// v1 tests: limit + market matching, price-time priority, partial
// fills, cancels.

#include <vector>

#include "check.hpp"
#include "om/orderbook.hpp"

using namespace om;

static Order limit(OrderId id, Side s, Price p, Qty q, TimestampNs ts) {
    return Order{id, s, OrderType::Limit, p, 0, q, 0, ts};
}
static Order market(OrderId id, Side s, Qty q, TimestampNs ts) {
    return Order{id, s, OrderType::Market, 0, 0, q, 0, ts};
}

TEST_CASE(limit_orders_rest_until_crossed) {
    OrderBook b;
    std::vector<Trade> ts;
    b.add(limit(1, Side::Buy, 100, 10, 1), ts);
    b.add(limit(2, Side::Sell, 101, 5, 2), ts);
    REQUIRE(ts.empty());
    REQUIRE_EQ(*b.best_bid(), 100);
    REQUIRE_EQ(*b.best_ask(), 101);

    // Cross: buy 7 at 101 → fills 5 at 101, rests 2 at 101? No, the
    // resting order is sell at 101, buyer's limit is 101, so 5 fill
    // at 101 (only 5 available); remaining 2 of the buyer rest at 101.
    b.add(limit(3, Side::Buy, 101, 7, 3), ts);
    REQUIRE_EQ(ts.size(), (size_t)1);
    REQUIRE_EQ(ts[0].price, (Price)101);
    REQUIRE_EQ(ts[0].qty, (Qty)5);
    REQUIRE_EQ(*b.best_bid(), 101);
    REQUIRE_EQ(b.depth_at(Side::Buy, 101), (Qty)2);
    REQUIRE(!b.best_ask().has_value());
}

TEST_CASE(market_orders_sweep_levels) {
    OrderBook b;
    std::vector<Trade> ts;
    b.add(limit(1, Side::Sell, 100, 5, 1), ts);
    b.add(limit(2, Side::Sell, 101, 5, 2), ts);
    b.add(limit(3, Side::Sell, 102, 5, 3), ts);

    // Market buy 12 sweeps 100, 101, partial 102.
    b.add(market(99, Side::Buy, 12, 4), ts);
    REQUIRE_EQ(ts.size(), (size_t)3);
    REQUIRE_EQ(ts[0].price, (Price)100);
    REQUIRE_EQ(ts[1].price, (Price)101);
    REQUIRE_EQ(ts[2].price, (Price)102);
    REQUIRE_EQ(ts[2].qty, (Qty)2);
    REQUIRE_EQ(b.depth_at(Side::Sell, 102), (Qty)3);
}

TEST_CASE(price_time_priority_within_level) {
    OrderBook b;
    std::vector<Trade> ts;
    b.add(limit(1, Side::Buy, 100, 5, 1), ts);  // earlier
    b.add(limit(2, Side::Buy, 100, 5, 2), ts);  // later
    b.add(limit(3, Side::Sell, 100, 7, 3), ts);

    // Earlier order (id=1) fills first, fully (5). Then id=2 fills 2.
    REQUIRE_EQ(ts.size(), (size_t)2);
    REQUIRE_EQ(ts[0].maker, (OrderId)1);
    REQUIRE_EQ(ts[0].qty, (Qty)5);
    REQUIRE_EQ(ts[1].maker, (OrderId)2);
    REQUIRE_EQ(ts[1].qty, (Qty)2);
    REQUIRE_EQ(b.depth_at(Side::Buy, 100), (Qty)3);
}

TEST_CASE(cancel_removes_order) {
    OrderBook b;
    std::vector<Trade> ts;
    b.add(limit(1, Side::Buy, 100, 10, 1), ts);
    REQUIRE_EQ(b.size(), (size_t)1);
    REQUIRE(b.cancel(1));
    REQUIRE_EQ(b.size(), (size_t)0);
    REQUIRE(!b.best_bid().has_value());
    REQUIRE(!b.cancel(1));  // double-cancel is a no-op
}

TEST_CASE(market_order_unfilled_does_not_rest) {
    OrderBook b;
    std::vector<Trade> ts;
    b.add(limit(1, Side::Sell, 100, 3, 1), ts);
    b.add(market(2, Side::Buy, 10, 2), ts);
    REQUIRE_EQ(ts.size(), (size_t)1);
    REQUIRE_EQ(ts[0].qty, (Qty)3);
    REQUIRE_EQ(b.size(), (size_t)0);  // 7 unfilled was dropped, not rested
}

int main() { return check::run(); }
