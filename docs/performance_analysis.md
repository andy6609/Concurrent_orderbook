# Performance Analysis

This note interprets the current benchmark data. For the correctness defects,
benchmark redesign, exact before/after comparison, and verification commands, see
[Correctness and Benchmark Revision](correctness_and_benchmark_revision.md).

## Test environment and evidence level

- macOS 26.5, ARM64
- Apple Clang 21.0.0, C++17
- Release build; the order-book library is compiled with `-O3`
- fixed random seeds
- five trials for lock and sharding tests; seven trials for storage tests
- alternating variant order; median reported
- inputs generated outside timing; throughput and latency measured on separate
  fresh books
- raw trials preserved in `results/*_trials.csv`

These tests establish observations on one machine. Explanations about lock
internals, allocator behaviour, or cache traffic remain hypotheses because no
profiler or hardware-counter trace was collected in this audit.

## Mutex versus shared mutex

The benchmark performs two best-price reads for each read operation and adds a
limit order for each write operation. Critical sections are short.

At eight threads:

| Workload | `std::mutex` | `std::shared_mutex` | Mutex advantage | mutex p99 | shared p99 |
|---|---:|---:|---:|---:|---:|
| 90% reads | 7.64M ops/s | 2.22M ops/s | 3.45× | 35.2 μs | 112.1 μs |
| 50% reads | 2.37M ops/s | 1.04M ops/s | 2.28× | 57.9 μs | 166.5 μs |
| 20% reads | 1.55M ops/s | 0.85M ops/s | 1.82× | 74.4 μs | 172.7 μs |

The safe conclusion is narrow: on this machine and workload, permitting concurrent
readers does not repay the extra read/write-lock cost.

Plausible mechanisms include shared reader-state contention, fairness policy, and
the fact that the protected read is only a best-price lookup. Those mechanisms
should be described as hypotheses in interviews unless validated with Instruments,
`perf`, or equivalent evidence.

Useful follow-up experiments:

- increase the amount of work performed under each read lock to find a crossover;
- run on Linux/x86 and the intended deployment CPU;
- pin threads and control background load/power state;
- collect lock wait time, cache misses, context switches, and scheduler events.

## Pooled order storage

The corrected test isolates `Order` object storage. Both variants use identical
price maps, pointer lists, ID-to-iterator indices, and O(1) cancellation. The
throughput and per-operation latency measurements are separate so clock calls do
not contaminate throughput. Pool construction/preallocation is outside timing.

| Storage | Median throughput | Median p50 | Median p99 |
|---|---:|---:|---:|
| `OrderPool` | 19.35M ops/s | 42 ns | 167 ns |
| Heap `new`/`delete` | 15.35M ops/s | 42 ns | 208 ns |

The pool improves median throughput by 26.1% and lowers p99 by 19.7% in this test.
That is useful but far below the retired 8.9× claim, which was confounded by an
O(n) baseline cancellation path and undefined behaviour.

The pool does not make the engine allocation-free. `std::list`, `std::map`,
`std::unordered_map`, and returned trade vectors may still allocate. The precise
claim is that it removes individual heap allocation for the `Order` object itself.

## Per-symbol sharding

The controlled comparison gives both variants four independent books and the same
four-symbol stream. One variant makes all four books share one process-wide mutex;
the other gives each symbol book its own mutex.

| Threads | Global lock | Per-symbol locks | Ratio |
|---:|---:|---:|---:|
| 1 | 15.62M ops/s | 14.02M ops/s | 0.90× |
| 2 | 7.67M ops/s | 6.92M ops/s | 0.90× |
| 4 | 4.25M ops/s | 5.96M ops/s | 1.40× |
| 8 | 2.72M ops/s | 3.03M ops/s | 1.11× |

At one thread, sharding's routing/registry overhead costs throughput. Under
contention it becomes beneficial in this run from four threads, peaking at 1.40×,
then measuring 1.11× at eight threads. The result does not justify claiming linear
scaling or assuming every multithreaded point benefits.

The sharded path still acquires a shared registry lock before entering the symbol
book, which limits how isolated the hot paths are. A fixed symbol table, immutable
registry after startup, or routing outside the matching layer would be useful
follow-up designs.

## Interview-safe summary

The strongest defensible story is not “I made it 9× faster.” It is:

> I audited my own benchmark and found it mixed allocator changes with a different
> cancellation algorithm and undefined behaviour. I redesigned it so only order
> storage differed. The honest result was 1.26× throughput and 19.7% lower p99 on
> my Mac. I made the same correction to the sharding benchmark, kept raw trials,
> and separated measured facts from causal hypotheses.

That demonstrates experimental discipline, understanding of confounders, and the
ability to revise a result when the evidence changes.
