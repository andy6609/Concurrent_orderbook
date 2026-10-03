#include "order.h"
#include "order_pool.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Isolate one variable: storage for Order objects. Both books use the same
// containers, hash index, and O(1) iterator-based cancel path. Only OrderPool
// versus heap new/delete differs.

class PoolStorage {
public:
    explicit PoolStorage(std::size_t capacity) : pool_(capacity) {}
    Order* allocate(const Order& order) { return pool_.allocate(order); }
    void deallocate(Order* order) { pool_.deallocate(order); }

private:
    OrderPool pool_;
};

class HeapStorage {
public:
    explicit HeapStorage(std::size_t) {}
    Order* allocate(const Order& order) { return new Order(order); }
    void deallocate(Order* order) { delete order; }
};

template <typename Storage>
class AllocationBenchBook {
public:
    explicit AllocationBenchBook(std::size_t capacity) : storage_(capacity) {
        orders_.reserve(capacity);
    }

    bool add(const Order& order) {
        if (orders_.find(order.id) != orders_.end()) return false;

        Order* stored = storage_.allocate(order);
        if (stored == nullptr) return false;

        auto& levels = order.side == Side::BUY ? bids_ : asks_;
        auto& level = levels[order.price];
        const auto position = level.insert(level.end(), stored);
        orders_.emplace(order.id, Entry{order.side, order.price, position});
        return true;
    }

    bool cancel(uint64_t order_id) {
        auto found = orders_.find(order_id);
        if (found == orders_.end()) return false;

        const Entry entry = found->second;
        auto& levels = entry.side == Side::BUY ? bids_ : asks_;
        auto level = levels.find(entry.price);
        if (level == levels.end()) throw std::logic_error("order index points to missing level");

        Order* stored = *entry.position;
        level->second.erase(entry.position);
        if (level->second.empty()) levels.erase(level);
        orders_.erase(found);
        storage_.deallocate(stored);
        return true;
    }

private:
    using Level = std::list<Order*>;
    struct Entry {
        Side side;
        uint64_t price;
        typename Level::iterator position;
    };

    Storage storage_;
    std::map<uint64_t, Level> bids_;
    std::map<uint64_t, Level> asks_;
    std::unordered_map<uint64_t, Entry> orders_;
};

using Clock = std::chrono::steady_clock;

struct Result {
    std::string allocator;
    int trial;
    uint64_t throughput_ops_per_sec;
    uint64_t p50_latency_ns;
    uint64_t p99_latency_ns;
};

static constexpr std::size_t N_ORDERS = 300'000;
static constexpr std::size_t N_LATENCY_ORDERS = 50'000;
static constexpr int N_TRIALS = 7;

std::vector<Order> make_orders(std::size_t count) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> price_dist(9'900, 10'100);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::vector<Order> orders;
    orders.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Side side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
        orders.push_back(Order::Limit(i + 1, 1, side, price_dist(rng), 10));
    }
    return orders;
}

uint64_t percentile(std::vector<uint64_t> samples, double quantile) {
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(quantile * (samples.size() - 1));
    return samples[index];
}

template <typename Storage>
Result run_trial(const std::string& label, int trial,
                 const std::vector<Order>& throughput_orders,
                 const std::vector<Order>& latency_orders) {
    AllocationBenchBook<Storage> throughput_book(throughput_orders.size());

    const auto batch_start = Clock::now();
    for (const auto& order : throughput_orders) {
        if (!throughput_book.add(order)) throw std::runtime_error("add failed");
    }
    for (const auto& order : throughput_orders) {
        if (!throughput_book.cancel(order.id)) throw std::runtime_error("cancel failed");
    }
    const auto batch_end = Clock::now();

    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        batch_end - batch_start).count();
    const auto operation_count = throughput_orders.size() * 2;
    const uint64_t throughput = static_cast<uint64_t>(
        static_cast<long double>(operation_count) * 1'000'000'000.0L /
        static_cast<long double>(elapsed_ns));

    // Separate latency run: per-operation clock calls do not distort throughput.
    AllocationBenchBook<Storage> latency_book(latency_orders.size());
    std::vector<uint64_t> samples;
    samples.reserve(latency_orders.size() * 2);

    for (const auto& order : latency_orders) {
        const auto start = Clock::now();
        const bool added = latency_book.add(order);
        const auto end = Clock::now();
        if (!added) throw std::runtime_error("latency add failed");
        samples.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }
    for (const auto& order : latency_orders) {
        const auto start = Clock::now();
        const bool cancelled = latency_book.cancel(order.id);
        const auto end = Clock::now();
        if (!cancelled) throw std::runtime_error("latency cancel failed");
        samples.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }

    return Result{label, trial, throughput,
                  percentile(samples, 0.50), percentile(samples, 0.99)};
}

uint64_t median(std::vector<uint64_t> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

Result summarize(const std::string& label, const std::vector<Result>& trials) {
    std::vector<uint64_t> throughput;
    std::vector<uint64_t> p50;
    std::vector<uint64_t> p99;
    for (const auto& result : trials) {
        if (result.allocator != label) continue;
        throughput.push_back(result.throughput_ops_per_sec);
        p50.push_back(result.p50_latency_ns);
        p99.push_back(result.p99_latency_ns);
    }
    return Result{label, 0, median(throughput), median(p50), median(p99)};
}

int main() {
    const auto throughput_orders = make_orders(N_ORDERS);
    const auto latency_orders = make_orders(N_LATENCY_ORDERS);

    std::cout << "Controlled allocation benchmark\n"
              << "Same containers + same O(1) cancel; only Order storage differs\n"
              << "orders=" << N_ORDERS << ", latency_orders=" << N_LATENCY_ORDERS
              << ", trials=" << N_TRIALS << "\n\n";

    std::vector<Result> trials;
    trials.reserve(N_TRIALS * 2);
    for (int trial = 1; trial <= N_TRIALS; ++trial) {
        // Alternate execution order to reduce systematic thermal/order bias.
        if (trial % 2 == 1) {
            trials.push_back(run_trial<PoolStorage>("OrderPool", trial,
                                                    throughput_orders, latency_orders));
            trials.push_back(run_trial<HeapStorage>("Heap new/delete", trial,
                                                    throughput_orders, latency_orders));
        } else {
            trials.push_back(run_trial<HeapStorage>("Heap new/delete", trial,
                                                    throughput_orders, latency_orders));
            trials.push_back(run_trial<PoolStorage>("OrderPool", trial,
                                                    throughput_orders, latency_orders));
        }
    }

    const Result pool = summarize("OrderPool", trials);
    const Result heap = summarize("Heap new/delete", trials);

    auto print = [](const Result& result) {
        std::cout << result.allocator
                  << ": median throughput=" << result.throughput_ops_per_sec << " ops/s"
                  << ", median p50=" << result.p50_latency_ns << " ns"
                  << ", median p99=" << result.p99_latency_ns << " ns\n";
    };
    print(pool);
    print(heap);
    std::cout << "Throughput ratio (pool/heap): "
              << static_cast<double>(pool.throughput_ops_per_sec) /
                 static_cast<double>(heap.throughput_ops_per_sec)
              << "x\n";

    std::filesystem::create_directories("results");
    std::ofstream trial_csv("results/pool_trials.csv");
    trial_csv << "allocator,trial,n_orders,throughput_ops_per_sec,p50_latency_ns,p99_latency_ns\n";
    for (const auto& result : trials) {
        trial_csv << result.allocator << ',' << result.trial << ',' << N_ORDERS << ','
                  << result.throughput_ops_per_sec << ',' << result.p50_latency_ns << ','
                  << result.p99_latency_ns << '\n';
    }

    std::ofstream summary_csv("results/pool_results.csv");
    summary_csv << "allocator,n_orders,n_trials,throughput_ops_per_sec,p50_latency_ns,p99_latency_ns\n";
    for (const auto& result : {pool, heap}) {
        summary_csv << result.allocator << ',' << N_ORDERS << ',' << N_TRIALS << ','
                    << result.throughput_ops_per_sec << ',' << result.p50_latency_ns << ','
                    << result.p99_latency_ns << '\n';
    }

    std::cout << "Results saved to results/pool_results.csv and results/pool_trials.csv\n";
}
