# Correctness and Benchmark Revision — 26 September 2026

## Scope

This audit rechecked two separate questions:

1. Does the matching engine preserve its stated order semantics at boundary cases?
2. Do the benchmarks isolate the optimisation they claim to measure?

The answer was “not completely” for both. This document records the failures,
their causes, the fixes, and the fresh measurements. It deliberately retains the
old advertised numbers for comparison rather than silently replacing them.

## Executive comparison

| Area | Before | Re-audited result |
|---|---|---|
| Limit-price safety | Uncovered bug allowed a limit order to sweep beyond its limit | BUY and SELL boundaries enforced at every level |
| SELL FOK depth check | Traversed bids in ascending map order and could stop incorrectly | Traverses best bid to lower bids, stopping below limit |
| Pool exhaustion | A null allocation could be inserted and the request reported accepted | Non-storable GTC requests reject without mutation |
| Release tests | `assert` could be disabled by `NDEBUG` | Test target explicitly undefines `NDEBUG` |
| Concurrent correctness | “No crash” only | Exact 40k add and 40k cancel totals verified |
| Pool benchmark | 8.9× headline | 1.26× median throughput in a controlled comparison |
| Sharding benchmark | 2.4× headline; 1.94× README table | 1.11× at 8 threads with equal four-book semantics |
| Mutex benchmark | Single-run 3.6× read-heavy claim | Controlled five-trial median: mutex 3.45× faster at 8T/90% reads |

## Correctness findings

### 1. Limit orders crossed beyond their limit

Reproduction before the fix:

```text
resting asks: 100 × 5, 110 × 5
incoming: IOC BUY limit 100 × 10
old result: trades at 100 and 110
expected: trade only at 100; cancel the remaining 5
```

The old code checked whether the order crossed the best price once, then reused a
market-order matching loop. That loop did not recheck the incoming limit at later
price levels.

The matching loop now checks every selected level:

- BUY stops when the best remaining ask is above the limit.
- SELL stops when the best remaining bid is below the limit.

Regression tests cover both directions.

### 2. SELL FOK scanned the bid book in the wrong direction

The bid map is ascending, while executable priority for a SELL is descending. The
old FOK pre-check iterated from `begin()`, so it could encounter a low,
non-executable bid and stop before seeing a higher executable bid.

The depth calculation is now shared by GTC/IOC/FOK logic and scans:

- asks from lowest to highest for BUY;
- bids from highest to lowest for SELL;
- only levels inside the incoming limit.

The accumulated quantity saturates at the incoming quantity to avoid unsigned
overflow when summing large resting quantities.

### 3. Pool exhaustion was not propagated

`OrderPool::allocate` correctly returned `nullptr` when full, but the old
`OrderBook` insertion path did not check it. A non-marketable GTC request could be
reported as accepted and leave a null pointer in a price level.

The corrected policy is:

- reject a GTC order before mutation when it needs a new slot and no matching can
  free one;
- allow a fully marketable order because it does not need to rest;
- allow a partially marketable order to reuse a slot freed by consuming resting
  liquidity;
- return `accepted == false` with no trades for the rejected case.

Regression tests cover all three paths with a pool capacity of one.

### 4. Release-mode tests could report success without checking assertions

The suite is written with standard `assert`. CMake Release builds conventionally
define `NDEBUG`, which removes those expressions. The test target now compiles with
`-UNDEBUG` on GCC/Clang and `/UNDEBUG` on MSVC so Release benchmark builds still run
the assertions.

### 5. The concurrency test was too weak

The old test mixed racy guesses about which ID to cancel and only checked that a
read call did not crash afterward. It could not establish the expected state.

The revised test assigns each of four threads a disjoint ID range, verifies all
40,000 adds succeed and the exact order count is 40,000, then concurrently cancels
those same ranges and verifies all cancels succeed and the final count is zero.

## Benchmark problems and corrections

### Common methodology changes

The current benchmarks use `std::chrono::steady_clock`, fixed input seeds, inputs
generated outside timing, variant execution order that alternates each trial, and
median summaries. Throughput and latency run on separate fresh books so per-op
clock calls do not alter the throughput number. Raw trials are written alongside
the summary CSVs.

The measurements were run on 26 September 2026 with:

```text
macOS 26.5, ARM64
Apple Clang 21.0.0
CMake 4.4.0
Release build (-O3 for the orderbook library)
```

They are local microbenchmarks. They are evidence about this test setup, not a
latency or throughput promise for a trading system.

### Pool: why 8.9× was not trustworthy

The old benchmark did not vary only the allocator:

- the pooled production path used an iterator index for O(1) cancellation;
- the baseline searched a list with `remove_if`, making cancellation O(n);
- the baseline retained a pointer to an element and read through it after erasure,
  which is undefined behaviour;
- therefore the result mixed allocation, cancellation algorithm, container
  behaviour, and undefined behaviour.

The new benchmark gives both sides the same:

- `map<price, list<Order*>>` structure;
- unordered ID-to-iterator index;
- O(1) iterator erase;
- 300,000 generated orders per throughput trial;
- independent 50,000-order latency run.

Only `Order` storage differs: `OrderPool` versus heap `new`/`delete`. Pool
construction/preallocation occurs outside the timed section.

| Metric | Old presentation | Controlled pool | Controlled heap | New comparison |
|---|---:|---:|---:|---:|
| Throughput | 8.9× speedup | 19,347,973 ops/s | 15,347,754 ops/s | **1.2606×** |
| p50 | Not reported | 42 ns | 42 ns | equal |
| p99 | 7.5× claim | 167 ns | 208 ns | pool is **19.7% lower** |

The production book still allocates list nodes, map nodes, hash-table storage, and
trade-vector capacity. “Heap allocations eliminated” would therefore be false.

### Sharding: why the old comparison changed semantics

The old baseline sent all operations into one book, while the sharded variant sent
them to four symbol books. It therefore changed book state and contention topology
at the same time. Its stored CSV implied 2.44× at eight threads, while the README
table said 1.94× and the headline rounded this to 2.4×; the repository was
internally inconsistent.

The corrected benchmark creates four books on both sides and gives both variants
the exact same four-symbol, all-BUY order stream:

- baseline: four books whose lock policy delegates to one process-wide mutex;
- sharded: four books with one mutex per symbol;
- symbol creation is outside the timed region;
- 50,000 operations per thread, five trials, median reported.

| Threads | Global mutex | Per-symbol locks | Ratio |
|---:|---:|---:|---:|
| 1 | 15,620,528 | 14,019,507 | 0.8975× |
| 2 | 7,670,892 | 6,923,569 | 0.9026× |
| 4 | 4,249,024 | 5,956,879 | 1.4019× |
| 8 | 2,724,972 | 3,029,889 | **1.1119×** |

The controlled result still supports sharding under contention, but the measured
gain is modest on this workload and machine.

### Mutex: single run replaced with medians

The original result CSV held one run per configuration. The revised benchmark runs
each policy five times, alternates which policy runs first, and retains every trial.
The documented read-heavy mix is corrected from 95/5 to the actual source setting,
90/10.

Eight-thread median results:

| Workload | mutex | shared_mutex | mutex/shared | mutex p99 | shared p99 |
|---|---:|---:|---:|---:|---:|
| 90% reads | 7,637,668 | 2,216,601 | **3.45×** | 35,209 ns | 112,084 ns |
| 50% reads | 2,368,646 | 1,039,916 | **2.28×** | 57,917 ns | 166,458 ns |
| 20% reads | 1,547,556 | 849,022 | **1.82×** | 74,417 ns | 172,666 ns |

The data establishes the measured outcome, not its microarchitectural cause.
Reader-count contention and heavier read/write-lock machinery are reasonable
hypotheses, but Instruments, `perf`, or hardware-counter evidence would be needed
to present them as demonstrated causes.

## Verification performed

Release build and assertion-enabled suite:

```bash
cmake -S . -B /private/tmp/orderbook-verify-20260926 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /private/tmp/orderbook-verify-20260926 -j4
/private/tmp/orderbook-verify-20260926/test_correctness
```

Result: all 24 order-book cases passed for both `MutexPolicy` and
`SharedMutexPolicy`; all three pool cases and all four sharding cases passed.

Sanitizer build:

```bash
cmake -S . -B /private/tmp/orderbook-sanitize-20260926 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /private/tmp/orderbook-sanitize-20260926 -j4 \
  --target test_correctness
/private/tmp/orderbook-sanitize-20260926/test_correctness
```

Result: full suite passed with no AddressSanitizer or UndefinedBehaviorSanitizer
diagnostic.

Benchmarks:

```bash
/private/tmp/orderbook-verify-20260926/bench_comparison
/private/tmp/orderbook-verify-20260926/bench_pool
/private/tmp/orderbook-verify-20260926/bench_sharding
```

## Remaining limits

- Assertions are suitable for this small project but a production-grade suite
  should use a test framework that reports individual failures without depending
  on compiler macros.
- The concurrent test checks final state and successful operations; it is not a
  formal linearizability proof or a ThreadSanitizer run.
- Benchmarks should be repeated on Linux/x86 and target deployment hardware.
- Confidence intervals, CPU affinity, warm-up policy, power state, and system load
  are not controlled.
- No profiler evidence has yet established the exact lock or cache bottleneck.
