// v2: market data feed — top-of-book + trade tape.
//
// `Feed` wraps an `OrderBook` and emits snapshots on every state
// change. Subscribers register a callback or drain the queued events.
// In production the events would be serialized and pushed onto a UDP
// multicast or a Kafka topic; we keep this in-memory so the test
// surface is hermetic.

#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "om/order.hpp"
#include "om/orderbook.hpp"

namespace om {

struct L1Snapshot {
    std::optional<Price> bid;
    Qty bid_qty;
    std::optional<Price> ask;
    Qty ask_qty;
    TimestampNs ts;
};

struct FeedEvent {
    enum Kind { Trade, L1 } kind;
    om::Trade trade;       // valid when kind == Trade
    L1Snapshot l1;         // valid when kind == L1
};

class Feed {
   public:
    explicit Feed(OrderBook& book) : book_(book) {}

    // Apply a new order, capture trades, emit Trade + L1 events.
    void on_order(const Order& o) {
        std::vector<Trade> trades;
        book_.add(o, trades);
        for (const auto& t : trades) emit_trade(t);
        emit_l1(o.ts);
    }

    void on_cancel(OrderId id, TimestampNs ts) {
        if (book_.cancel(id)) emit_l1(ts);
    }

    // Drain queued frames in subscriber-visible form. The wire format
    // matches the SSE convention used by sensorflow + datachat.
    std::vector<std::string> drain_sse() {
        std::vector<std::string> out;
        out.reserve(events_.size());
        for (const auto& e : events_) {
            if (e.kind == FeedEvent::Trade) {
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                              "event: trade\ndata: {\"px\":%lld,\"qty\":%llu,\"ts\":%llu}\n\n",
                              (long long)e.trade.price,
                              (unsigned long long)e.trade.qty,
                              (unsigned long long)e.trade.ts);
                out.emplace_back(buf);
            } else {
                char buf[256];
                std::snprintf(
                    buf, sizeof(buf),
                    "event: l1\ndata: {\"bid\":%s,\"ask\":%s,\"ts\":%llu}\n\n",
                    e.l1.bid ? std::to_string(*e.l1.bid).c_str() : "null",
                    e.l1.ask ? std::to_string(*e.l1.ask).c_str() : "null",
                    (unsigned long long)e.l1.ts);
                out.emplace_back(buf);
            }
        }
        events_.clear();
        return out;
    }

    const std::vector<FeedEvent>& events() const { return events_; }

   private:
    OrderBook& book_;
    std::vector<FeedEvent> events_;

    void emit_trade(const Trade& t) {
        FeedEvent e{};
        e.kind = FeedEvent::Trade;
        e.trade = t;
        events_.push_back(e);
    }

    void emit_l1(TimestampNs ts) {
        L1Snapshot s{};
        s.ts = ts;
        s.bid = book_.best_bid();
        s.ask = book_.best_ask();
        s.bid_qty = s.bid ? book_.depth_at(Side::Buy, *s.bid) : 0;
        s.ask_qty = s.ask ? book_.depth_at(Side::Sell, *s.ask) : 0;
        FeedEvent e{};
        e.kind = FeedEvent::L1;
        e.l1 = s;
        events_.push_back(e);
    }
};

}  // namespace om
