# Concurrent Order Book

A C++17 matching engine exploring lock policy, memory allocation, and horizontal scaling tradeoffs.

The project has two branches:
- **`main`** — original v1: lock policy comparison (mutex vs shared_mutex)
- **`v2-upgrade`** — extended v2: full matching engine + memory pool + per-symbol sharding

**v1 question:** When does `std::shared_mutex` actually outperform `std::mutex` in a contended order book?

**v2 additions:** Real limit-limit crossing with trade events, IOC/FOK order types, a pre-allocated memory pool (8.9× throughput gain), and per-symbol sharding (2× gain at 8 threads).

**Short answer on locking:** On this setup (macOS, Apple Silicon), `std::mutex` won in every configuration. `shared_mutex` p99 latency exploded under contention — 99μs vs 30μs at 8 threads on a read-heavy workload. The shared_mutex fairness/reader-count overhead exceeded the benefit of concurrent reads.

---

## Results

![Throughput Comparison](results/throughput_comparison.png)

![p99 Latency Comparison](results/p99_latency_comparison.png)

### Key numbers (8 threads)

| Workload | mutex throughput | shared_mutex throughput | mutex p99 | shared_mutex p99 |
|----------|-----------------|------------------------|-----------|------------------|
| read_heavy (95/5) | 5.4M ops/sec | 1.5M ops/sec | 30 μs | 99 μs |
| balanced (70/20/10) | 3.4M ops/sec | 1.1M ops/sec | 37 μs | 138 μs |
| write_heavy (20/30/50) | 2.8M ops/sec | 1.1M ops/sec | 41 μs | 138 μs |

### Why shared_mutex lost

The critical sections in this order book are short — `best_bid_price()` is a single `map::rbegin()` dereference. When the protected work is cheap, the lock's internal overhead dominates. `shared_mutex` maintains an atomic reader count and fairness logic that `mutex` doesn't need. Under high contention with many threads, that overhead compounds.

This is platform-dependent. Linux's `pthread_rwlock` implementation may behave differently, especially on NUMA hardware where reader scalability matters more. The result here is specific to macOS/Apple Silicon, which is part of the point: you have to measure.

---

## Architecture

### v2 (current — `v2-upgrade` branch)

```
ShardedOrderBook<LockPolicy>
└── books_  : unordered_map<symbol_id, OrderBook>  // per-symbol, independent locks

OrderBook<LockPolicy>
├── pool_   : OrderPool                             // pre-allocated contiguous slots
├── bids_   : map<price, list<Order*>>              // pointers into pool
├── asks_   : map<price, list<Order*>>              // pointers into pool
└── orders_ : unordered_map<id, Order*>             // O(1) cancel lookup

OrderPool
├── slots_    : Slot[]    // contiguous array, sizeof(Order) each
└── free_list_: size_t[]  // O(1) stack of available indices
```

### v1 (original — `main` branch)

```
OrderBook<LockPolicy>
├── bids_   : map<price, list<Order>>   // owns Order objects directly
├── asks_   : map<price, list<Order>>   // owns Order objects directly
└── orders_ : unordered_map<id, Order*> // O(1) cancel lookup
```

Lock policy is a compile-time template parameter — same logic, only the lock type changes:

```cpp
OrderBook<MutexPolicy>       // std::mutex — all ops exclusive
OrderBook<SharedMutexPolicy> // std::shared_mutex — reads shared, writes exclusive
```

---

## Order types (v2)

- **Limit GTC:** rests on the book at a specified price; matches aggressively if it crosses
- **Limit IOC:** matches immediately, cancels any unfilled remainder (never rests)
- **Limit FOK:** fills entirely in one shot or is cancelled — pre-checks available quantity
- **Market:** matches immediately against resting orders, best price first

Price-time priority: best price first, FIFO within the same price level.

---

## Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Requires C++17, CMake 3.10+, pthreads.

## Run

```bash
./test_correctness    # all correctness tests (ShardedOrderBook, OrderPool, matching)
./bench_comparison    # mutex vs shared_mutex → results/benchmark_results.csv
./bench_pool          # OrderPool vs default allocator
./bench_sharding      # single book vs ShardedOrderBook across thread counts
python3 scripts/plot_results.py   # generate graphs from bench_comparison CSV
```

---

## Correctness tests

| Test | What it verifies |
|------|-----------------|
| Add limit order | Best bid/ask update; duplicate ID rejected |
| Price-time priority | FIFO within same price level |
| Cancel order | Order removed; double-cancel returns false |
| Market order matching | Crosses price levels, partial fills |
| Partial fill | Resting order stays on book until fully consumed |
| Multi-level cross | Market order sweeps multiple price levels in order |
| Cancel nonexistent | Returns false, no crash |
| Empty book queries | best_bid/ask return nullopt; total_orders returns 0 |
| Cancel updates best price | Cancelling best-price order exposes next level |
| Concurrent add + cancel | 4 threads, 40k ops — no crash, no deadlock |

Both `OrderBook<MutexPolicy>` and `OrderBook<SharedMutexPolicy>` pass the same test suite.

---

## Benchmark configuration

- **Workloads:** read_heavy (95/5/0), balanced (70/20/10), write_heavy (20/30/50)
- **Thread counts:** 1, 2, 4, 8
- **Ops per thread:** 100,000
- **Seed:** fixed (42) for reproducibility
- **Metrics:** throughput (ops/sec), avg latency (ns), p99 latency (ns)
- **Platform:** macOS, Apple Silicon

---

## v2 benchmark highlights

| Benchmark | Result |
|---|---|
| OrderPool vs heap alloc (500k orders) | **8.9× throughput**, 8.9× avg latency |
| Sharding gain at 8 threads (4 symbols) | **1.94×** vs single global lock |
| Single book degradation: 1T → 8T | −61% (lock contention) |
| Sharded book degradation: 1T → 8T | −4% (near-flat) |

See `docs/performance_analysis.md` for full breakdown and analysis.

## What I'd do next

- Run on Linux (x86, NUMA) to see if shared_mutex or pool characteristics differ
- Try a spinlock policy as a third lock comparison
- Lock-free free-list in OrderPool to remove the last serialisation point
- Longer critical sections (e.g., order validation, logging) where shared_mutex overhead is amortized
