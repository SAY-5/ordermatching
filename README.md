# ordermatching

Header-only C++20 price-time priority matching engine with a streaming
market-data feed and advanced order types (Stop, StopLimit, Iceberg).

```
┌──────────┐  Order   ┌────────────┐  Trade   ┌──────────┐
│ gateway  │ ──────▶ │   Engine   │ ───────▶ │   feed   │
└──────────┘          │ (OrderBook │          │ L1 + tape│
                      │  + Stops + │          └──────────┘
                      │  Iceberg)  │
                      └────────────┘
```

## Versions

| Version | Capability | Status |
|---|---|---|
| v1 | Limit + Market orders, price-time priority, partial fills, cancels | shipped |
| v2 | Streaming market data feed: L1 snapshots + trade tape (SSE-formatted) | shipped |
| v3 | Stop / StopLimit (last-price triggered) + Iceberg (auto-refresh slices) | shipped |

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Or directly:

```bash
clang++ -std=c++20 -O2 -Iinclude src/matchcli.cpp -o matchcli
./matchcli
```

## Tests

11 tests across 3 binaries:

- `test_orderbook` — limit/market matching, FIFO priority, cancels (5)
- `test_stream` — feed events + SSE frame format (3)
- `test_advanced` — stop triggers + iceberg refresh (3)

## Why integer prices

Floats break `std::map` ordering on NaN and accumulate rounding drift
on every cumulative-volume calculation. Real exchanges quote in
integer ticks; we follow the same convention so a future port to a
production engine doesn't have to retrofit it.

See `ARCHITECTURE.md` for the order-book layout, iceberg refresh
protocol, and stop-trigger semantics.
