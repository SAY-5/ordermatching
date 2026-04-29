// v3: Engine — wraps OrderBook + Feed and adds advanced order types.
//
// Stop / StopLimit: parked outside the book until last-trade-price
// crosses `stop_price`, then injected as Market / Limit. We hold them
// in two sorted multimaps keyed on stop_price so triggering is O(log
// N) per crossing.
//
// Iceberg: only `display_qty` is visible at any time. The Engine
// tracks the residual `(total - display)` and re-issues a fresh slice
// after each fill until the order is exhausted or cancelled.

#pragma once
#include <map>
#include <unordered_map>
#include <vector>

#include "om/feed.hpp"
#include "om/order.hpp"
#include "om/orderbook.hpp"

namespace om {

class Engine {
   public:
    Engine() : feed_(book_) {}

    // Submit an incoming order. Returns the trades produced (cleared
    // each call). Stops & icebergs return empty trades on submission;
    // their fills happen later when the trigger fires / when their
    // visible slice gets matched.
    std::vector<Trade> submit(const Order& o) {
        if (o.type == OrderType::Stop || o.type == OrderType::StopLimit) {
            stops_.insert({o.stop_price, o});
            return {};
        }
        if (o.type == OrderType::Iceberg) {
            return submit_iceberg(o);
        }
        size_t before = feed_.events().size();
        feed_.on_order(o);
        auto trades = trades_since(before);
        update_last_price(trades);
        refresh_consumed_icebergs(trades);
        process_stops();
        return trades;
    }

    bool cancel(OrderId id, TimestampNs ts) {
        // Engine-managed orders (icebergs / stops) live outside the
        // book until/while only a slice is resting. We have to look in
        // multiple places.
        if (cancel_iceberg(id)) return true;
        if (cancel_stop(id)) return true;
        feed_.on_cancel(id, ts);
        return true;
    }

    OrderBook& book() { return book_; }
    Feed& feed() { return feed_; }

    std::optional<Price> last_price() const { return last_price_; }

   private:
    OrderBook book_;
    Feed feed_;
    std::optional<Price> last_price_;

    // Stops: keyed on stop_price. Buys trigger when last >= stop_price;
    // sells trigger when last <= stop_price.
    std::multimap<Price, Order> stops_;

    // Iceberg state: the *visible slice* sits on the book under
    // OrderId == display_id. We track display_id → (parent_id, residual)
    // and parent_id → display_id for cancels.
    struct IceState {
        OrderId parent;
        Order parent_order;
        Qty residual;
    };
    std::unordered_map<OrderId, IceState> icebergs_by_display_;
    std::unordered_map<OrderId, OrderId> display_by_parent_;
    OrderId next_display_id_ = (OrderId(1) << 62);

    std::vector<Trade> trades_since(size_t marker) {
        std::vector<Trade> out;
        const auto& evs = feed_.events();
        for (size_t i = marker; i < evs.size(); ++i) {
            if (evs[i].kind == FeedEvent::Trade) out.push_back(evs[i].trade);
        }
        return out;
    }

    void update_last_price(const std::vector<Trade>& ts) {
        if (!ts.empty()) last_price_ = ts.back().price;
    }

    void process_stops() {
        if (!last_price_) return;
        Price lp = *last_price_;
        std::vector<Order> ready;
        for (auto it = stops_.begin(); it != stops_.end();) {
            const Order& s = it->second;
            bool hit = (s.side == Side::Buy && lp >= s.stop_price) ||
                       (s.side == Side::Sell && lp <= s.stop_price);
            if (hit) {
                Order o = s;
                o.type = (s.type == OrderType::Stop) ? OrderType::Market : OrderType::Limit;
                ready.push_back(o);
                it = stops_.erase(it);
            } else {
                ++it;
            }
        }
        // Inject. Recursion through submit() is fine — the new orders
        // can themselves trigger more stops.
        for (const auto& o : ready) submit(o);
    }

    std::vector<Trade> submit_iceberg(const Order& o) {
        if (o.display_qty == 0 || o.display_qty >= o.qty) {
            // No icebergness needed: act like a plain limit.
            Order plain = o;
            plain.type = OrderType::Limit;
            feed_.on_order(plain);
            return {};
        }
        return reslice_iceberg(o, o.qty);
    }

    std::vector<Trade> reslice_iceberg(const Order& parent, Qty residual) {
        Order slice = parent;
        slice.type = OrderType::Limit;
        slice.qty = std::min(parent.display_qty, residual);
        OrderId display_id = next_display_id_++;
        slice.id = display_id;
        size_t before = feed_.events().size();
        feed_.on_order(slice);
        auto trades = trades_since(before);

        // What's left in the iceberg that's NOT YET on the book:
        // residual minus the slice's *committed* quantity (slice.qty).
        Qty hidden_residual = residual - slice.qty;

        if (!book_.has(display_id)) {
            // Slice fully consumed during its own submission (either
            // as a taker that crossed resting orders, or as a maker
            // immediately swept by a contra-side order — though the
            // latter can't happen on initial submit). Reslice now.
            if (hidden_residual > 0) {
                auto more = reslice_iceberg(parent, hidden_residual);
                trades.insert(trades.end(), more.begin(), more.end());
            }
        } else {
            // The slice is resting (possibly partially filled). Track
            // the hidden residual for refresh on later fills.
            icebergs_by_display_[display_id] =
                IceState{parent.id, parent, hidden_residual};
            display_by_parent_[parent.id] = display_id;
        }
        update_last_price(trades);
        process_stops();
        return trades;
    }

    // After a submit produced trades, walk the maker side: any maker
    // matching an iceberg-display id needs its residual decremented;
    // if the slice is fully consumed, drop a fresh slice.
    void refresh_consumed_icebergs(const std::vector<Trade>& trades) {
        // Aggregate fills per display id (a single submit can chew
        // through multiple slices).
        std::unordered_map<OrderId, Qty> consumed;
        for (const auto& t : trades) consumed[t.maker] += t.qty;

        std::vector<std::pair<Order, Qty>> to_reslice;
        for (auto& [display, q] : consumed) {
            auto it = icebergs_by_display_.find(display);
            if (it == icebergs_by_display_.end()) continue;
            // If the slice is still resting, it was only partially
            // filled — leave it. We only refresh once it's gone.
            if (book_.has(display)) continue;
            Order parent = it->second.parent_order;
            Qty hidden_residual = it->second.residual;
            display_by_parent_.erase(it->second.parent);
            icebergs_by_display_.erase(it);
            if (hidden_residual > 0) to_reslice.emplace_back(parent, hidden_residual);
        }
        for (auto& [parent, residual] : to_reslice) {
            reslice_iceberg(parent, residual);
        }
    }

    bool cancel_iceberg(OrderId parent_id) {
        auto it = display_by_parent_.find(parent_id);
        if (it == display_by_parent_.end()) return false;
        OrderId display = it->second;
        feed_.on_cancel(display, 0);
        icebergs_by_display_.erase(display);
        display_by_parent_.erase(it);
        return true;
    }

    bool cancel_stop(OrderId id) {
        for (auto it = stops_.begin(); it != stops_.end(); ++it) {
            if (it->second.id == id) {
                stops_.erase(it);
                return true;
            }
        }
        return false;
    }
};

}  // namespace om
