#include "order_book.h"
#include "sharded_order_book.h"
#include <thread>
#include <fstream>
#include <filesystem>
#include <vector>
#include <chrono>
#include <iostream>
#include <atomic>
#include <random>
#include <numeric>
#include <algorithm>

// ── Config ────────────────────────────────────────────────────────────────────
static constexpr int THREAD_COUNTS[] = {1, 2, 4, 8};
static constexpr int OPS_PER_THREAD  = 50'000;

// ── Result ────────────────────────────────────────────────────────────────────
struct Result {
    const char* label;
    int         n_symbols;
    int         threads;
    long        throughput_ops_per_sec;
    long        avg_ns;
    long        p99_ns;
};

// ── Single global OrderBook (all symbols on one book) ─────────────────────────
static std::atomic<uint64_t> g_id{1};

void worker_single(ExclusiveOrderBook* book, int n_ops, int tid,
                   std::vector<double>& latencies)
{
    std::mt19937 rng(static_cast<uint32_t>(tid) * 31337u);
    std::uniform_int_distribution<uint64_t> price_dist(9900, 10100);

    latencies.reserve(n_ops);
    for (int i = 0; i < n_ops; ++i) {
        uint64_t id    = g_id.fetch_add(1, std::memory_order_relaxed);
        uint64_t price = price_dist(rng);
        Side     side  = (id % 2 == 0) ? Side::BUY : Side::SELL;

        auto t0 = std::chrono::high_resolution_clock::now();
        book->add_order(Order::Limit(id, 1, side, price, 10));
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
}

void worker_sharded(ShardedOrderBook<MutexPolicy>* shard, int n_ops, int tid,
                    int n_symbols, std::vector<double>& latencies)
{
    std::mt19937 rng(static_cast<uint32_t>(tid) * 31337u);
    std::uniform_int_distribution<uint64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<uint32_t> sym_dist(1, static_cast<uint32_t>(n_symbols));

    latencies.reserve(n_ops);
    for (int i = 0; i < n_ops; ++i) {
        uint64_t  id     = g_id.fetch_add(1, std::memory_order_relaxed);
        uint32_t  sym    = sym_dist(rng);
        uint64_t  price  = price_dist(rng);
        Side      side   = (id % 2 == 0) ? Side::BUY : Side::SELL;

        auto t0 = std::chrono::high_resolution_clock::now();
        shard->add_order(Order::Limit(id, sym, side, price, 10));
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
}

// ── Aggregation ───────────────────────────────────────────────────────────────
Result aggregate(const char* label, int n_symbols, int n_threads,
                 std::vector<std::vector<double>>& all_lat,
                 double elapsed_s)
{
    std::vector<double> flat;
    flat.reserve(static_cast<size_t>(n_threads) * OPS_PER_THREAD);
    for (auto& v : all_lat) flat.insert(flat.end(), v.begin(), v.end());
    std::sort(flat.begin(), flat.end());

    double avg = std::accumulate(flat.begin(), flat.end(), 0.0) / flat.size();
    double p99 = flat[flat.size() * 99 / 100];

    Result r;
    r.label                  = label;
    r.n_symbols              = n_symbols;
    r.threads                = n_threads;
    r.throughput_ops_per_sec = static_cast<long>(flat.size() / elapsed_s);
    r.avg_ns                 = static_cast<long>(avg);
    r.p99_ns                 = static_cast<long>(p99);
    return r;
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "========================================\n"
              << "Benchmark: Single Book vs Sharded (4 symbols)\n"
              << "ops_per_thread=" << OPS_PER_THREAD << "\n"
              << "========================================\n\n";

    const int N_SYMBOLS = 4;

    std::vector<Result> results;

    for (int tc : THREAD_COUNTS) {
        std::cout << "--- " << tc << " thread(s) ---\n";

        // Single global book
        {
            g_id.store(1, std::memory_order_relaxed);
            ExclusiveOrderBook single_book(static_cast<std::size_t>(tc) * OPS_PER_THREAD + 10);
            std::vector<std::thread>         threads;
            std::vector<std::vector<double>> lats(tc);
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int t = 0; t < tc; ++t)
                threads.emplace_back(worker_single, &single_book, OPS_PER_THREAD, t, std::ref(lats[t]));
            for (auto& th : threads) th.join();
            auto t1 = std::chrono::high_resolution_clock::now();
            double el = std::chrono::duration<double>(t1 - t0).count();
            auto r = aggregate("single", 1, tc, lats, el);
            results.push_back(r);
            std::cout << "  [single] tput=" << r.throughput_ops_per_sec
                      << " ops/s | avg=" << r.avg_ns << " ns | p99=" << r.p99_ns << " ns\n";
        }

        // Sharded book
        {
            g_id.store(1, std::memory_order_relaxed);
            ShardedOrderBook<MutexPolicy> shard(static_cast<std::size_t>(tc) * OPS_PER_THREAD + 10);
            std::vector<std::thread>         threads;
            std::vector<std::vector<double>> lats(tc);
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int t = 0; t < tc; ++t)
                threads.emplace_back(worker_sharded, &shard, OPS_PER_THREAD, t, N_SYMBOLS, std::ref(lats[t]));
            for (auto& th : threads) th.join();
            auto t1 = std::chrono::high_resolution_clock::now();
            double el = std::chrono::duration<double>(t1 - t0).count();
            auto r = aggregate("sharded", N_SYMBOLS, tc, lats, el);
            results.push_back(r);
            std::cout << "  [sharded] tput=" << r.throughput_ops_per_sec
                      << " ops/s | avg=" << r.avg_ns << " ns | p99=" << r.p99_ns << " ns\n";
        }
        std::cout << "\n";
    }

    // ── CSV output ────────────────────────────────────────────────────────────
    std::filesystem::create_directories("results");
    std::ofstream csv("results/sharding_results.csv");
    csv << "mode,n_symbols,threads,throughput_ops_per_sec,avg_latency_ns,p99_latency_ns\n";
    for (const auto& r : results)
        csv << r.label << "," << r.n_symbols << "," << r.threads << ","
            << r.throughput_ops_per_sec << "," << r.avg_ns << "," << r.p99_ns << "\n";
    std::cout << "Results saved -> results/sharding_results.csv\n";

    return 0;
}
