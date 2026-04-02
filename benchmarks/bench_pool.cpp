#include "order_book.h"
#include "order_pool.h"
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <algorithm>
#include <vector>
#include <list>
#include <map>
#include <unordered_map>
#include <mutex>

// ── Naive order book using std::list<Order> (no memory pool) ─────────────────
// Mirrors the essential add/cancel path of OrderBook but stores Order objects
// directly inside the list, triggering heap allocation on every push_back.
class NaiveBook {
public:
    bool add(const Order& order) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (index_.count(order.id)) return false;

        // Simple non-crossing insert (benchmarks resting-order allocation)
        auto& level = (order.side == Side::BUY ? bids_ : asks_)[order.price];
        level.push_back(order);
        index_[order.id] = &level.back();
        return true;
    }

    bool cancel(uint64_t id) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = index_.find(id);
        if (it == index_.end()) return false;

        Order* ptr = it->second;
        auto& levels = (ptr->side == Side::BUY) ? bids_ : asks_;
        auto& level  = levels[ptr->price];
        level.remove_if([id](const Order& o) { return o.id == id; });
        if (level.empty()) levels.erase(ptr->price);
        index_.erase(it);
        return true;
    }

private:
    std::mutex mtx_;
    std::map<uint64_t, std::list<Order>>  bids_;
    std::map<uint64_t, std::list<Order>>  asks_;
    std::unordered_map<uint64_t, Order*>  index_;
};

// ── Benchmark helpers ─────────────────────────────────────────────────────────
struct Result {
    const char* label;
    long        throughput_ops_per_sec;
    long        avg_ns;
    long        p99_ns;
};

static constexpr int N_ORDERS = 500'000;

template <typename AddFn, typename CancelFn>
Result run(const char* label, AddFn add_fn, CancelFn cancel_fn) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<int>      side_dist(0, 1);

    std::vector<double> latencies;
    latencies.reserve(N_ORDERS * 2);

    // Phase 1: add N_ORDERS resting orders
    for (int i = 1; i <= N_ORDERS; ++i) {
        Side     side  = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
        uint64_t price = price_dist(rng);
        Order    o     = Order::Limit(static_cast<uint64_t>(i), 1, side, price, 10);

        auto t0 = std::chrono::high_resolution_clock::now();
        add_fn(o);
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    // Phase 2: cancel all orders (also measures dealloc path)
    for (int i = 1; i <= N_ORDERS; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        cancel_fn(static_cast<uint64_t>(i));
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    std::sort(latencies.begin(), latencies.end());
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0)
                 / static_cast<double>(latencies.size());
    double p99 = latencies[static_cast<size_t>(latencies.size() * 99 / 100)];

    double total_s = std::accumulate(latencies.begin(), latencies.end(), 0.0) / 1e9;

    Result r;
    r.label                 = label;
    r.throughput_ops_per_sec= static_cast<long>(latencies.size() / total_s);
    r.avg_ns                = static_cast<long>(avg);
    r.p99_ns                = static_cast<long>(p99);
    return r;
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "========================================\n"
              << "Benchmark: OrderPool vs Default Allocator\n"
              << "n_orders=" << N_ORDERS << " (add + cancel each)\n"
              << "========================================\n\n";

    // Pool-backed OrderBook (current implementation)
    ExclusiveOrderBook pool_book;
    auto r_pool = run(
        "OrderPool (pre-allocated)",
        [&](const Order& o) { pool_book.add_order(o); },
        [&](uint64_t id)    { pool_book.cancel_order(id); }
    );

    // Naive book with default allocator (std::list<Order> + new/delete)
    NaiveBook naive;
    auto r_naive = run(
        "Default allocator (std::list<Order>)",
        [&](const Order& o) { naive.add(o); },
        [&](uint64_t id)    { naive.cancel(id); }
    );

    auto print = [](const Result& r) {
        std::cout << "  [" << r.label << "]\n"
                  << "    throughput : " << r.throughput_ops_per_sec << " ops/s\n"
                  << "    avg latency: " << r.avg_ns  << " ns\n"
                  << "    p99 latency: " << r.p99_ns  << " ns\n\n";
    };

    print(r_pool);
    print(r_naive);

    double speedup = static_cast<double>(r_pool.throughput_ops_per_sec)
                   / static_cast<double>(r_naive.throughput_ops_per_sec);
    std::cout << "Pool speedup: " << speedup << "x\n";

    return 0;
}
