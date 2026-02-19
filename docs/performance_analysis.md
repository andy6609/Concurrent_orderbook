# Performance Analysis

Detailed notes on why `std::mutex` outperformed `std::shared_mutex` in every configuration I tested.

This isn't a definitive answer — it's what I observed on one platform and my best explanation for it.

## Setup

- macOS, Apple Silicon (ARM64)
- Apple Clang, C++17, -O3
- All benchmarks use fixed seed (42) for reproducibility

## The surprise

I expected `shared_mutex` to win on read-heavy workloads. The whole point of a read-write lock is that readers don't block each other. With a 95/5 read/write mix and 8 threads, shared_mutex should have had 7+ threads reading concurrently most of the time.

Instead, `mutex` was 3.6× faster on throughput and had 3.3× lower p99 latency.

| | mutex | shared_mutex |
|---|---|---|
| Throughput (8T, read_heavy) | 5.4M ops/sec | 1.5M ops/sec |
| p99 latency (8T, read_heavy) | 30 μs | 99 μs |

## What I think is happening

### 1. The critical sections are too short

`best_bid_price()` does one thing: dereference `bids_.rbegin()`. That's a pointer chase into a red-black tree node. Maybe 10-20ns of actual work.

`std::mutex` on macOS (based on `os_unfair_lock`) is essentially a single atomic compare-and-swap. Very little overhead for a very short critical section.

`std::shared_mutex` has to do more: atomically increment a reader count on entry, decrement on exit, and check for pending writers. That's at least two atomic operations per read lock, plus memory barriers. When the actual work inside the lock is 10-20ns, the lock overhead dominates.

For a lock to be worth the shared_mutex complexity, the work inside needs to be long enough that the overhead is amortized. My critical sections are nowhere near that threshold.

### 2. Reader count contention

This is the part that's counterintuitive. Even though readers "don't block each other," they still contend on the same atomic reader count variable. Every `shared_lock` acquisition increments the count, every release decrements it. With 8 threads doing this in a tight loop, that atomic variable becomes a bottleneck — cache lines bouncing between cores.

`std::mutex` doesn't have this problem. It's a single atomic flag: either you own it or you don't. There's no shared counter to contend on.

### 3. macOS implementation specifics

I haven't dug into the Darwin source to confirm this, but macOS's `std::shared_mutex` is built on `pthread_rwlock`, which has fairness guarantees to prevent writer starvation. That fairness comes with overhead — likely some form of turn-taking or queuing that adds latency even when there are no writers waiting.

`std::mutex` on macOS uses `os_unfair_lock`, which is explicitly unfair (hence the name). No fairness overhead, just raw speed.

### 4. At single thread, the gap is small

At 1 thread there's no contention, so the difference is just the lock's own overhead:

| | mutex | shared_mutex |
|---|---|---|
| read_heavy, 1T | 9.6M ops/sec | 8.3M ops/sec |
| balanced, 1T | 15.7M ops/sec | 14.3M ops/sec |

~15% difference. That's the baseline cost of shared_mutex's more complex lock/unlock path. It compounds as threads increase because every thread pays that cost and they start contending on the reader count.

## When would shared_mutex win?

I didn't test these, but based on the analysis above:

Longer critical sections. If the read path did real work — scanning a price level, computing VWAP, building a snapshot — the amortized lock overhead would be smaller relative to the work. At some critical section length, the concurrency benefit should overtake the overhead.

Linux with a different pthread_rwlock. Linux's implementation on glibc may have different tradeoffs. I've seen reports of pthread_rwlock scaling better on x86 NUMA systems where cache coherency costs are higher and reader concurrency has a bigger payoff.

Fewer threads. The reader count contention is worst at high thread counts. At 2 threads, the throughput gap is much smaller (8.8M vs 7.3M on read_heavy). There might be a sweet spot at low thread counts with moderately long critical sections.

## What I'd measure next

- Same benchmark on Linux x86 to isolate the platform variable
- Artificially lengthening the critical section (e.g., add a dummy loop inside the lock) to find the crossover point where shared_mutex starts winning
- A spinlock policy as a third comparison point
- `perf stat` or Instruments profiling to see actual cache miss counts and context switches
