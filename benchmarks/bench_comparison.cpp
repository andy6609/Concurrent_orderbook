#include "order_book.h"
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
#include <fstream>
#include <atomic>
#include <random>
#include <algorithm>
#include <numeric>
#include <string>
#include <filesystem>

// ── Workload definitions ──────────────────────────────────────────────────────
struct WorkloadConfig {
    const char* name;
    int read_pct;   // percentage of ops that are reads (0-100)
};

static constexpr WorkloadConfig WORKLOADS[] = {
    {"read_heavy",  90},
    {"balanced",    50},
    {"write_heavy", 20},
};

static constexpr int THREAD_COUNTS[] = {1, 2, 4, 8};
static constexpr int OPS_PER_THREAD  = 100'000;

// ── Result record ─────────────────────────────────────────────────────────────
struct BenchResult {
    std::string workload;
    std::string policy;
    int         threads;
    uint64_t    total_ops;
    long        throughput_ops_per_sec;
    long        avg_latency_ns;
    long        p99_latency_ns;
};

// ── Per-thread worker ─────────────────────────────────────────────────────────
static std::atomic<uint64_t> g_next_order_id{1};

template <typename LockPolicy>
void worker(OrderBook<LockPolicy>* book,
            int num_ops,
            int thread_id,
            int read_pct,
            std::vector<double>& latencies)
{
    std::mt19937 rng(static_cast<uint32_t>(thread_id) * 1234567u + 42u);
    std::uniform_int_distribution<uint64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);
    std::uniform_int_distribution<int>      side_dist(0, 1);
    std::uniform_int_distribution<int>      op_dist(0, 99);

    latencies.reserve(num_ops);

    for (int i = 0; i < num_ops; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();

        if (op_dist(rng) < read_pct) {
            // Read path
            book->best_bid_price();
            book->best_ask_price();
        } else {
            // Write path
            uint64_t id    = g_next_order_id.fetch_add(1, std::memory_order_relaxed);
            Side     side  = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
            uint64_t price = price_dist(rng);
            uint64_t qty   = qty_dist(rng);
            book->add_order(Order::Limit(id, 1, side, price, qty));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
}

// ── Single benchmark run ──────────────────────────────────────────────────────
template <typename LockPolicy>
BenchResult run_one(const WorkloadConfig& wl,
                    int                   num_threads,
                    const char*           policy_name)
{
    OrderBook<LockPolicy> book;
    g_next_order_id.store(1, std::memory_order_relaxed);

    std::vector<std::thread>          threads;
    std::vector<std::vector<double>>  all_latencies(num_threads);

    auto wall_start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker<LockPolicy>,
                             &book, OPS_PER_THREAD, t,
                             wl.read_pct,
                             std::ref(all_latencies[t]));
    }
    for (auto& th : threads) th.join();

    auto wall_end   = std::chrono::high_resolution_clock::now();
    double elapsed  = std::chrono::duration<double>(wall_end - wall_start).count();

    // Aggregate and sort latencies
    std::vector<double> flat;
    flat.reserve(static_cast<size_t>(num_threads) * OPS_PER_THREAD);
    for (auto& v : all_latencies)
        flat.insert(flat.end(), v.begin(), v.end());

    std::sort(flat.begin(), flat.end());

    double avg = std::accumulate(flat.begin(), flat.end(), 0.0)
                 / static_cast<double>(flat.size());
    double p99 = flat[static_cast<size_t>(flat.size() * 99 / 100)];

    BenchResult r;
    r.workload              = wl.name;
    r.policy                = policy_name;
    r.threads               = num_threads;
    r.total_ops             = flat.size();
    r.throughput_ops_per_sec= static_cast<long>(flat.size() / elapsed);
    r.avg_latency_ns        = static_cast<long>(avg);
    r.p99_latency_ns        = static_cast<long>(p99);
    return r;
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main()
{
    std::vector<BenchResult> results;
    results.reserve(24);   // 3 workloads × 4 thread_counts × 2 policies

    std::cout << "========================================\n"
              << "Benchmark Comparison: Mutex vs SharedMutex\n"
              << "ops_per_thread=" << OPS_PER_THREAD << "\n"
              << "========================================\n\n";

    for (const auto& wl : WORKLOADS) {
        std::cout << "=== Workload: " << wl.name
                  << " (read=" << wl.read_pct << "% write=" << (100 - wl.read_pct) << "%) ===\n";

        for (int tc : THREAD_COUNTS) {
            auto r1 = run_one<MutexPolicy>      (wl, tc, "MutexPolicy");
            auto r2 = run_one<SharedMutexPolicy>(wl, tc, "SharedMutexPolicy");
            results.push_back(r1);
            results.push_back(r2);

            auto print = [](const BenchResult& r) {
                std::cout << "  [" << r.policy << "]"
                          << " threads=" << r.threads
                          << " | ops="   << r.total_ops
                          << " | tput="  << r.throughput_ops_per_sec << " ops/s"
                          << " | avg="   << r.avg_latency_ns << " ns"
                          << " | p99="   << r.p99_latency_ns << " ns"
                          << "\n";
            };
            print(r1);
            print(r2);
        }
        std::cout << "\n";
    }

    // ── CSV output ────────────────────────────────────────────────────────────
    std::filesystem::create_directories("results");
    std::ofstream csv("results/benchmark_results.csv");
    csv << "workload,policy,threads,total_ops,"
           "throughput_ops_per_sec,avg_latency_ns,p99_latency_ns\n";
    for (const auto& r : results) {
        csv << r.workload              << ","
            << r.policy               << ","
            << r.threads              << ","
            << r.total_ops            << ","
            << r.throughput_ops_per_sec << ","
            << r.avg_latency_ns       << ","
            << r.p99_latency_ns       << "\n";
    }

    std::cout << "Results saved → results/benchmark_results.csv\n"
              << "Total rows: " << results.size() << "\n";
    return 0;
}
