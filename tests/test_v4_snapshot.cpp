// v4 tests: EventLog records adds + cancels in seq order; replay
// rebuilds state.

#include <cstdio>
#include <vector>

#include "check.hpp"
#include "om/orderbook.hpp"
#include "om/snapshot.hpp"

using namespace om;
using namespace om::snapshot;

static Order limit(OrderId id, Side s, Price p, Qty q, TimestampNs ts) {
    return Order{id, s, OrderType::Limit, p, 0, q, 0, ts};
}

TEST_CASE(eventlog_records_adds_and_cancels_in_order) {
    EventLog log;
    log.on_add(limit(1, Side::Buy, 100, 5, 1));
    log.on_add(limit(2, Side::Sell, 101, 3, 2));
    log.on_cancel(1);
    REQUIRE_EQ(log.events().size(), (size_t)3);
    REQUIRE(log.events()[0].kind == EventKind::Add);
    REQUIRE(log.events()[2].kind == EventKind::Cancel);
    REQUIRE_EQ(log.events()[2].cancel_id, (OrderId)1);
    REQUIRE_EQ(log.last_seq(), (uint64_t)3);
}

TEST_CASE(replay_from_log_rebuilds_book_state) {
    // Source-of-truth book: apply orders directly, capture state.
    OrderBook source;
    std::vector<Trade> trades;
    EventLog log;
    auto add = [&](OrderId id, Side s, Price p, Qty q) {
        Order o = limit(id, s, p, q, 0);
        log.on_add(o);
        source.add(o, trades);
    };
    add(1, Side::Buy, 100, 10);
    add(2, Side::Buy, 99, 5);
    add(3, Side::Sell, 101, 7);
    log.on_cancel(2);
    source.cancel(2);

    // Recovered book: replay from the log into a fresh book.
    OrderBook recovered;
    std::vector<Trade> recover_trades;
    for (const auto& ev : log.events()) {
        if (ev.kind == EventKind::Add) recovered.add(ev.order, recover_trades);
        else recovered.cancel(ev.cancel_id);
    }
    REQUIRE_EQ(recovered.size(), source.size());
    REQUIRE_EQ(*recovered.best_bid(), *source.best_bid());
    REQUIRE_EQ(*recovered.best_ask(), *source.best_ask());
}

TEST_CASE(write_jsonl_emits_one_line_per_event) {
    EventLog log;
    log.on_add(limit(1, Side::Buy, 100, 5, 1));
    log.on_cancel(1);
    char buf[1024];
    FILE* f = fmemopen(buf, sizeof(buf), "w");
    log.write_jsonl(f);
    fclose(f);
    int newlines = 0;
    for (size_t i = 0; buf[i]; ++i) if (buf[i] == '\n') ++newlines;
    REQUIRE_EQ(newlines, 2);
    REQUIRE(std::string(buf).find("\"kind\":\"add\"") != std::string::npos);
    REQUIRE(std::string(buf).find("\"kind\":\"cancel\"") != std::string::npos);
}

int main() { return check::run(); }
