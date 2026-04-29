// v2 tests: market data feed emits L1 + Trade events on every state
// change. SSE frame format is verified by string-search.

#include <string>
#include <vector>

#include "check.hpp"
#include "om/feed.hpp"
#include "om/orderbook.hpp"

using namespace om;

static Order limit(OrderId id, Side s, Price p, Qty q, TimestampNs ts) {
    return Order{id, s, OrderType::Limit, p, 0, q, 0, ts};
}

TEST_CASE(feed_emits_l1_on_each_event) {
    OrderBook b;
    Feed f(b);
    f.on_order(limit(1, Side::Buy, 100, 5, 1));
    REQUIRE_EQ(f.events().size(), (size_t)1);  // 1 L1 (no trade)
    REQUIRE(f.events()[0].kind == FeedEvent::L1);
    REQUIRE_EQ(*f.events()[0].l1.bid, (Price)100);

    f.on_order(limit(2, Side::Sell, 100, 3, 2));
    // 1 trade + 1 L1
    REQUIRE_EQ(f.events().size(), (size_t)3);
    REQUIRE(f.events()[1].kind == FeedEvent::Trade);
    REQUIRE_EQ(f.events()[1].trade.qty, (Qty)3);
}

TEST_CASE(sse_frame_format_is_correct) {
    OrderBook b;
    Feed f(b);
    f.on_order(limit(1, Side::Buy, 50, 1, 1));
    auto frames = f.drain_sse();
    REQUIRE_EQ(frames.size(), (size_t)1);
    REQUIRE(frames[0].find("event: l1\n") == 0);
    REQUIRE(frames[0].find("\"bid\":50") != std::string::npos);
    REQUIRE(frames[0].find("\n\n") != std::string::npos);
}

TEST_CASE(drain_clears_buffer) {
    OrderBook b;
    Feed f(b);
    f.on_order(limit(1, Side::Buy, 50, 1, 1));
    f.drain_sse();
    auto second = f.drain_sse();
    REQUIRE(second.empty());
}

int main() { return check::run(); }
