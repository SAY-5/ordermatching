# Architecture

## Order book layout (v1)

Two `std::map<Price, std::list<Order>>` instances — one per side. Bids
use `std::greater<Price>` so the best bid is `bids_.begin()`; asks use
the default `std::less<Price>` so the best ask is `asks_.begin()`.

Within each price level, `std::list<Order>` preserves FIFO arrival
order. An `unordered_map<OrderId, Loc>` index lets cancel hit O(log P)
where P is the number of distinct price levels.

A production engine would replace `std::list` with an intrusive doubly
linked list per level (no per-order allocation) and the `std::map`
with a flat array keyed on relative price (since exchange tick sizes
are bounded). For correctness + clarity at this version level the
STL-only path is the right call.

## Matching loop

```
while taker.qty > 0 and book has crossing levels:
    take the best price level
    for each order in FIFO order at that level:
        fill = min(taker.qty, maker.qty)
        emit Trade(taker, maker, level_price, fill)
        decrement both qtys
        if maker.qty == 0: remove from book
```

Market orders skip the cross-check (they accept any price). Limit
orders break out when the next level no longer crosses the limit.
Unfilled market orders are dropped; unfilled limit orders rest on the
book.

## Feed layer (v2)

`Feed` wraps the book. Every `on_order` / `on_cancel` produces:

- One `Trade` event per match (in order)
- One `L1` snapshot at the end (best bid + best ask + their sizes)

Frames are buffered until `drain_sse()` is called, which formats them
as standard SSE strings (`event: <type>\ndata: <json>\n\n`). The
framework-free design means a Starlette/FastAPI/Crow handler is a
3-line `for frame in feed.drain_sse(): yield frame`.

## Engine (v3)

### Stops

Stops are parked in a `std::multimap<stop_price, Order>`. After every
trade the engine updates `last_price_` and walks the multimap looking
for triggered stops. A buy-stop triggers when `last >= stop_price`; a
sell-stop when `last <= stop_price`. Triggered stops convert to Market
(for `Stop`) or Limit (for `StopLimit`) and are submitted recursively.
The recursion is safe because the stops map is mutated *before* the
recursive call, so the same stop can't trigger twice.

### Iceberg

An iceberg order with `display_qty < total_qty` only exposes a slice
to the book. The engine tracks `(parent_id, residual_hidden_qty)` per
display slice. When a slice is fully consumed (i.e., `book.has(display)
== false`), the engine drops a fresh slice carved from the residual.
Refresh is triggered from two paths:

1. The slice was fully consumed *during its own submission* (it
   crossed resting orders on entry). Handled by recursion in
   `reslice_iceberg`.
2. The slice was consumed *by a future incoming order*. Handled by
   `refresh_consumed_icebergs` which runs at the end of `submit()`.

The key insight: detection uses `book.has(display)` rather than
counting trade quantities, because the slice can be either taker or
maker depending on entry order, and counting only one side misses
half the cases.

## Tradeoffs we deliberately made

- **No persistence.** Real exchanges journal every event for crash
  recovery. We don't — adding it is a 50-line write-ahead log.
- **No fee schedule.** Fees, rebates, and self-trade prevention are
  exchange-policy concerns and don't affect the matching primitive.
- **No threading.** A production engine partitions by symbol; the
  per-symbol matching path is single-threaded for determinism. Our
  in-process tests don't need partitioning so we skip the dispatcher.
