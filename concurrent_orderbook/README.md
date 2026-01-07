# Concurrent Order Book (C++17)

A small, well-scoped limit order book in modern C++17 built to study **correctness-first design** and **locking trade-offs** under contention.
It implements price–time priority matching, then gradually adds synchronization to support safe concurrent access.

> This is a focused prototype (not a production exchange engine)

---

## What this project covers

- **Price–Time Priority matching**
  - Best price first, FIFO within a price level
- **Thread safety**
  - Safe concurrent reads and writes via `std::shared_mutex`
- **Locking trade-offs (the real point)**
  - Measures how performance changes with threads and read/write ratio
- **Order operations**
  - Limit / Market / Cancel (intentionally minimal)

---

## Architecture (high-level)

```
OrderBook
├── Price levels
│   ├── bids_ : sorted by price (desc when querying best bid)
│   └── asks_ : sorted by price (asc when querying best ask)
└── Order index
    └── orders_ : id -> pointer/reference to the resting order
```

### Concurrency model

- **Read path** (`best_bid_price`, `best_ask_price`, `total_orders`)
  - `std::shared_lock<std::shared_mutex>`
  - many readers can proceed concurrently
- **Write path** (`add_order`, `cancel_order`, matching updates)
  - `std::unique_lock<std::shared_mutex>`
  - exclusive access

This is designed to be easy to reason about and to benchmark. The intent is to compare "simple exclusive locking" vs "read-write locking" behavior under different workloads.

---

## Build

### Requirements
- C++17 compiler (GCC / Clang / MSVC)
- CMake 3.10+
- pthreads (Linux/macOS)

### Build steps
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

---

## Testing

### Correctness tests
```bash
./test_correctness
```

The tests focus on:
- price–time priority (FIFO within the same price)
- matching semantics (partial fill / full fill)
- cancel behavior
- regression safety while changing synchronization strategy

### Benchmark
```bash
./bench_performance
```

Benchmarks measure throughput and average latency across different thread counts.
Workload is configurable; the sample below uses 70% reads / 30% writes.

**Sample results (Release, 70/30 workload)**
```
Threads: 1 | Ops: 100000 | Time: 12ms  | Throughput: 8,333,333 ops/sec | Avg latency: 120 ns
Threads: 2 | Ops: 200000 | Time: 35ms  | Throughput: 5,714,285 ops/sec | Avg latency: 175 ns
Threads: 4 | Ops: 400000 | Time: 93ms  | Throughput: 4,301,075 ops/sec | Avg latency: 232 ns
Threads: 8 | Ops: 800000 | Time: 532ms | Throughput: 1,503,759 ops/sec | Avg latency: 665 ns
```

**Interpretation (important)**
- 1 thread: strong baseline performance (no contention).
- More threads: under this particular 70/30 mix, shared_mutex overhead and lock contention become visible.
- Takeaway: read–write locks are not "always faster"; the benefit depends heavily on workload and critical section size.

This project treats that outcome as a result, not a failure: it's exactly the sort of trade-off you want to be able to explain.

---

## Example usage

```cpp
#include "order_book.h"

OrderBook book;

book.add_order(Order::Limit(1, SYMBOL_ID, Side::BUY, 10050, 100));
book.add_order(Order::Limit(2, SYMBOL_ID, Side::SELL, 10100, 50));

auto bid = book.best_bid_price();
auto ask = book.best_ask_price();

book.add_order(Order::Market(3, SYMBOL_ID, Side::BUY, 25)); // matches immediately

book.cancel_order(1);
```

---

## Limitations (intentional)

- in-memory only (no persistence)
- single-symbol per order book instance
- minimal order types (no Stop/IOC/FOK)
- not a production exchange engine — a focused concurrency + correctness prototype

---

## Possible next steps

If extending this project, the most natural next experiments are:
- symbol-level sharding (multiple books + per-symbol locks)
- measuring different read/write mixes (e.g., 90/10 vs 50/50)
- alternative containers / memory pools (only if needed and measurable)

---

## License

MIT License - see LICENSE file for details
