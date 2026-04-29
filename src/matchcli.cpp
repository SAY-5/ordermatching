// Smoke binary: streams a simple scripted scenario through the
// matching engine and prints the resulting trades. Production engines
// take input from a binary message bus; this is a sanity tool only.

#include <cstdio>
#include <string>
#include <vector>

#include "om/orderbook.hpp"

int main() {
    om::OrderBook book;
    std::vector<om::Trade> trades;

    auto add = [&](om::OrderId id, om::Side s, om::OrderType t, om::Price p, om::Qty q,
                   om::TimestampNs ts) {
        om::Order o{id, s, t, p, 0, q, 0, ts};
        book.add(o, trades);
    };

    add(1, om::Side::Buy, om::OrderType::Limit, 100, 10, 1);
    add(2, om::Side::Buy, om::OrderType::Limit, 99, 5, 2);
    add(3, om::Side::Sell, om::OrderType::Limit, 101, 8, 3);
    add(4, om::Side::Sell, om::OrderType::Market, 0, 12, 4);  // sweeps bids

    std::printf("trades:\n");
    for (const auto& t : trades) {
        std::printf("  taker=%llu maker=%llu px=%lld qty=%llu\n",
                    (unsigned long long)t.taker, (unsigned long long)t.maker,
                    (long long)t.price, (unsigned long long)t.qty);
    }
    auto bb = book.best_bid();
    auto ba = book.best_ask();
    std::printf("top: bid=%s ask=%s\n",
                bb ? std::to_string(*bb).c_str() : "-",
                ba ? std::to_string(*ba).c_str() : "-");
    return 0;
}
