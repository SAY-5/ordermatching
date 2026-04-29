// Price-time priority order book.
//
// Bids: descending price; Asks: ascending price. Within a price level,
// orders are FIFO by arrival time. Implementation is two `std::map`s
// of `std::list<Order>`. We keep an index `order_index_` from OrderId
// to (price, list-iterator) for O(log P) cancels.
//
// This is intentionally simpler than a production engine (which would
// use intrusive doubly-linked lists per price level + a flat array
// keyed on price for L1/L2 hot paths). For correctness and clarity at
// the v1 level, std::map<Price, std::list<Order>> is the right call.

#pragma once
#include <cassert>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "om/order.hpp"

namespace om {

class OrderBook {
   public:
    // Add a new order. Trades produced by immediate matches are
    // appended to `out_trades`. Returns the residual quantity left on
    // the book (0 if fully filled).
    Qty add(const Order& o, std::vector<Trade>& out_trades);

    // Cancel an order by id. Returns true if removed.
    bool cancel(OrderId id);

    // Best bid / best ask (top of book). nullopt if that side empty.
    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    // Aggregate quantity at a price level (for L2 snapshots).
    Qty depth_at(Side side, Price p) const;

    // Total open orders. Useful for tests + metrics.
    size_t size() const { return order_index_.size(); }

    // True if `id` is currently resting on the book.
    bool has(OrderId id) const { return order_index_.count(id) != 0; }

   private:
    // Sorted price levels. Bids descend; asks ascend.
    std::map<Price, std::list<Order>, std::greater<Price>> bids_;
    std::map<Price, std::list<Order>> asks_;

    // OrderId → (price-level-key, iter-into-list).
    struct Loc {
        Side side;
        Price price;
        std::list<Order>::iterator it;
    };
    std::unordered_map<OrderId, Loc> order_index_;

    // Internal: try to match `taker` against the opposite side. Mutates
    // `taker.qty`. Appends Trade events to `out`.
    template <typename Book>
    void match_side(Order& taker, Book& book, std::vector<Trade>& out);

    // Returns true if `taker` can match against best price `lvl_price`.
    static bool crosses(Side taker_side, Price taker_limit, Price lvl_price) {
        if (taker_side == Side::Buy) return taker_limit >= lvl_price;
        return taker_limit <= lvl_price;
    }

    static Trade make_trade(const Order& taker, const Order& maker, Price p, Qty q) {
        return Trade{taker.id, maker.id, p, q, taker.ts};
    }
};

inline Qty OrderBook::add(const Order& in, std::vector<Trade>& out) {
    Order o = in;

    if (o.side == Side::Buy) {
        match_side(o, asks_, out);
    } else {
        match_side(o, bids_, out);
    }

    if (o.qty == 0) return 0;

    // Market orders that didn't fully fill are dropped (they don't
    // rest). Limit (and iceberg/stop-limit when triggered) rest on the
    // book.
    if (o.type == OrderType::Market) return 0;

    auto place_in = [&](auto& book) {
        auto& lst = book[o.price];
        lst.push_back(o);
        auto it = std::prev(lst.end());
        order_index_[o.id] = Loc{o.side, o.price, it};
    };
    if (o.side == Side::Buy) {
        place_in(bids_);
    } else {
        place_in(asks_);
    }
    return o.qty;
}

template <typename Book>
inline void OrderBook::match_side(Order& taker, Book& book, std::vector<Trade>& out) {
    while (taker.qty > 0 && !book.empty()) {
        auto top_it = book.begin();
        Price lvl_price = top_it->first;
        if (taker.type != OrderType::Market &&
            !crosses(taker.side, taker.price, lvl_price))
            break;

        auto& lst = top_it->second;
        while (taker.qty > 0 && !lst.empty()) {
            Order& maker = lst.front();
            Qty fill = std::min(taker.qty, maker.qty);
            out.push_back(make_trade(taker, maker, lvl_price, fill));
            taker.qty -= fill;
            maker.qty -= fill;
            if (maker.qty == 0) {
                order_index_.erase(maker.id);
                lst.pop_front();
            }
        }
        if (lst.empty()) book.erase(top_it);
    }
}

inline bool OrderBook::cancel(OrderId id) {
    auto idx = order_index_.find(id);
    if (idx == order_index_.end()) return false;
    auto loc = idx->second;
    if (loc.side == Side::Buy) {
        auto lvl = bids_.find(loc.price);
        if (lvl != bids_.end()) {
            lvl->second.erase(loc.it);
            if (lvl->second.empty()) bids_.erase(lvl);
        }
    } else {
        auto lvl = asks_.find(loc.price);
        if (lvl != asks_.end()) {
            lvl->second.erase(loc.it);
            if (lvl->second.empty()) asks_.erase(lvl);
        }
    }
    order_index_.erase(idx);
    return true;
}

inline std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

inline std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

inline Qty OrderBook::depth_at(Side side, Price p) const {
    if (side == Side::Buy) {
        auto it = bids_.find(p);
        if (it == bids_.end()) return 0;
        Qty total = 0;
        for (const auto& o : it->second) total += o.qty;
        return total;
    }
    auto it = asks_.find(p);
    if (it == asks_.end()) return 0;
    Qty total = 0;
    for (const auto& o : it->second) total += o.qty;
    return total;
}

}  // namespace om
