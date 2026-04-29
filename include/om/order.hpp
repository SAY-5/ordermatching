// Order types for the price-time priority matching engine.
//
// Prices are integer ticks (no float). Quantities are non-negative.
// Order IDs are monotonically increasing; the matching engine assumes
// uniqueness (the gateway is responsible for that).
//
// v3 adds Stop, StopLimit, and Iceberg (display_qty < total_qty); v1
// keeps these enum values reserved so the enum layout stays stable.

#pragma once
#include <cstdint>

namespace om {

using OrderId = uint64_t;
using Price = int64_t;
using Qty = uint64_t;
using TimestampNs = uint64_t;

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t {
    Limit,      // v1
    Market,     // v1
    Stop,       // v3
    StopLimit,  // v3
    Iceberg,    // v3
};

struct Order {
    OrderId id{0};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{0};        // limit price (ignored for Market)
    Price stop_price{0};   // v3: trigger price for Stop / StopLimit
    Qty qty{0};            // remaining quantity
    Qty display_qty{0};    // v3: visible slice for Iceberg (0 == full visible)
    TimestampNs ts{0};     // arrival time, for FIFO ordering
};

struct Trade {
    OrderId taker;
    OrderId maker;
    Price price;
    Qty qty;
    TimestampNs ts;
};

}  // namespace om
