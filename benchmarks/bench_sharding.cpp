#include "order_book.h"
#include "sharded_order_book.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;

static constexpr int THREAD_COUNTS[] = {1, 2, 4, 8};
static constexpr int OPS_PER_THREAD = 50'000;
static constexpr int N_SYMBOLS = 4;
static constexpr int N_TRIALS = 5;

// Both benchmark sides own four independent books. This baseline makes their
// lock policy delegate to one process-wide mutex; the sharded variant uses one
// ordinary mutex per book.
class GlobalLockedBooks {
public:
    explicit GlobalLockedBooks(std::size_t pool_capacity_per_symbol)
        : pool_capacity_per_symbol_(pool_capacity_per_symbol) {}

    void create_symbols() {
        for (int symbol = 1; symbol <= N_SYMBOLS; ++symbol) {
            books_.emplace(static_cast<uint32_t>(symbol),
                           std::make_unique<OrderBook<ProcessWideMutexPolicy>>(
                               pool_capacity_per_symbol_));
        }
    }

    AddResult add_order(const Order& order) {
        return books_.at(order.symbol_id)->add_order(order);
    }

private:
    std::unordered_map<uint32_t,
                       std::unique_ptr<OrderBook<ProcessWideMutexPolicy>>> books_;
    std::size_t pool_capacity_per_symbol_;
};

struct Operation {
    uint64_t id;
    uint32_t symbol;
    uint64_t price;
};

struct Result {
    std::string mode;
    int trial;
    int n_symbols;
    int threads;
    uint64_t throughput_ops_per_sec;
    uint64_t p50_latency_ns;
    uint64_t p99_latency_ns;
};

std::vector<std::vector<Operation>> make_operations(int thread_count) {
    std::vector<std::vector<Operation>> operations(thread_count);
    for (int thread = 0; thread < thread_count; ++thread) {
        auto& thread_ops = operations[thread];
        thread_ops.reserve(OPS_PER_THREAD);
        std::mt19937 rng(static_cast<uint32_t>(thread) * 31'337u + 42u);
        std::uniform_int_distribution<uint64_t> price_dist(9'900, 10'100);
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            const uint64_t id = static_cast<uint64_t>(thread) * OPS_PER_THREAD + i + 1;
            const uint32_t symbol = static_cast<uint32_t>((thread + i) % N_SYMBOLS + 1);
            thread_ops.push_back(Operation{id, symbol, price_dist(rng)});
        }
    }
    return operations;
}

template <typename Books>
void execute_operations(Books& books,
                        const std::vector<Operation>& operations,
                        std::vector<uint64_t>* latency_samples,
                        std::atomic<int>& failures) {
    if (latency_samples != nullptr) latency_samples->reserve(operations.size());

    for (const auto& operation : operations) {
        const auto start = latency_samples != nullptr ? Clock::now() : Clock::time_point{};
        const auto result = books.add_order(Order::Limit(
            operation.id, operation.symbol, Side::BUY, operation.price, 10));
        const auto end = latency_samples != nullptr ? Clock::now() : Clock::time_point{};

        if (!result.accepted)
            failures.fetch_add(1, std::memory_order_relaxed);
        if (latency_samples != nullptr) {
            latency_samples->push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
    }
}

template <typename Books>
uint64_t measure_throughput(
    Books& books,
    const std::vector<std::vector<Operation>>& operations) {
    std::vector<std::thread> workers;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> failures{0};

    for (std::size_t thread = 0; thread < operations.size(); ++thread) {
        workers.emplace_back([&, thread]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            execute_operations(books, operations[thread], nullptr, failures);
        });
    }

    while (ready.load(std::memory_order_acquire) != static_cast<int>(workers.size()))
        std::this_thread::yield();
    const auto wall_start = Clock::now();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    const auto wall_end = Clock::now();

    if (failures.load(std::memory_order_relaxed) != 0)
        throw std::runtime_error("throughput benchmark operation was rejected");

    const uint64_t operation_count =
        static_cast<uint64_t>(operations.size()) * OPS_PER_THREAD;
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        wall_end - wall_start).count();
    return static_cast<uint64_t>(
        static_cast<long double>(operation_count) * 1'000'000'000.0L /
        static_cast<long double>(elapsed_ns));
}

uint64_t percentile(const std::vector<uint64_t>& sorted_samples, double quantile) {
    const auto index = static_cast<std::size_t>(
        quantile * static_cast<double>(sorted_samples.size() - 1));
    return sorted_samples[index];
}

template <typename Books>
std::pair<uint64_t, uint64_t> measure_latency(
    Books& books,
    const std::vector<std::vector<Operation>>& operations) {
    std::vector<std::thread> workers;
    std::vector<std::vector<uint64_t>> samples(operations.size());
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> failures{0};

    for (std::size_t thread = 0; thread < operations.size(); ++thread) {
        workers.emplace_back([&, thread]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            execute_operations(books, operations[thread], &samples[thread], failures);
        });
    }

    while (ready.load(std::memory_order_acquire) != static_cast<int>(workers.size()))
        std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();

    if (failures.load(std::memory_order_relaxed) != 0)
        throw std::runtime_error("latency benchmark operation was rejected");

    std::vector<uint64_t> flat;
    flat.reserve(operations.size() * OPS_PER_THREAD);
    for (const auto& thread_samples : samples)
        flat.insert(flat.end(), thread_samples.begin(), thread_samples.end());
    std::sort(flat.begin(), flat.end());
    return {percentile(flat, 0.50), percentile(flat, 0.99)};
}

std::unique_ptr<GlobalLockedBooks> make_global_books(std::size_t pool_capacity) {
    auto books = std::make_unique<GlobalLockedBooks>(pool_capacity);
    books->create_symbols();
    return books;
}

std::unique_ptr<ShardedOrderBook<MutexPolicy>> make_sharded_books(
    std::size_t pool_capacity) {
    auto books = std::make_unique<ShardedOrderBook<MutexPolicy>>(pool_capacity);
    // Pre-create registry entries outside the timed region.
    for (int symbol = 1; symbol <= N_SYMBOLS; ++symbol) {
        const uint64_t id = 1'000'000'000ULL + static_cast<uint64_t>(symbol);
        books->add_order(Order::Limit(
            id, static_cast<uint32_t>(symbol), Side::BUY, 1, 1));
        books->cancel_order(static_cast<uint32_t>(symbol), id);
    }
    return books;
}

template <typename Factory>
Result run_trial(const std::string& mode,
                 int trial,
                 int thread_count,
                 const std::vector<std::vector<Operation>>& operations,
                 Factory make_books) {
    auto throughput_books = make_books();
    const uint64_t throughput = measure_throughput(*throughput_books, operations);
    throughput_books.reset();

    auto latency_books = make_books();
    const auto [p50, p99] = measure_latency(*latency_books, operations);
    return Result{mode, trial, N_SYMBOLS, thread_count, throughput, p50, p99};
}

uint64_t median(std::vector<uint64_t> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

Result summarize(const std::string& mode,
                 int thread_count,
                 const std::vector<Result>& trials) {
    std::vector<uint64_t> throughput;
    std::vector<uint64_t> p50;
    std::vector<uint64_t> p99;
    for (const auto& result : trials) {
        if (result.mode != mode || result.threads != thread_count) continue;
        throughput.push_back(result.throughput_ops_per_sec);
        p50.push_back(result.p50_latency_ns);
        p99.push_back(result.p99_latency_ns);
    }
    return Result{mode, 0, N_SYMBOLS, thread_count, median(throughput),
                  median(p50), median(p99)};
}

int main() {
    std::cout << "Controlled sharding benchmark\n"
              << "Four books on both sides; global mutex vs per-symbol mutexes\n"
              << "Inputs generated outside timing; throughput and latency use separate books\n"
              << "ops_per_thread=" << OPS_PER_THREAD << ", trials=" << N_TRIALS
              << " (reported values are medians)\n\n";

    std::vector<Result> trials;
    std::vector<Result> summaries;

    for (int thread_count : THREAD_COUNTS) {
        const auto operations = make_operations(thread_count);
        const std::size_t pool_capacity =
            static_cast<std::size_t>(thread_count) * OPS_PER_THREAD / N_SYMBOLS + 1'000;

        for (int trial = 1; trial <= N_TRIALS; ++trial) {
            auto global_factory = [pool_capacity]() {
                return make_global_books(pool_capacity);
            };
            auto sharded_factory = [pool_capacity]() {
                return make_sharded_books(pool_capacity);
            };

            if (trial % 2 == 1) {
                trials.push_back(run_trial(
                    "global", trial, thread_count, operations, global_factory));
                trials.push_back(run_trial(
                    "sharded", trial, thread_count, operations, sharded_factory));
            } else {
                trials.push_back(run_trial(
                    "sharded", trial, thread_count, operations, sharded_factory));
                trials.push_back(run_trial(
                    "global", trial, thread_count, operations, global_factory));
            }
        }

        const auto global = summarize("global", thread_count, trials);
        const auto sharded = summarize("sharded", thread_count, trials);
        summaries.push_back(global);
        summaries.push_back(sharded);

        std::cout << thread_count << " thread(s): global="
                  << global.throughput_ops_per_sec << " ops/s, sharded="
                  << sharded.throughput_ops_per_sec << " ops/s, ratio="
                  << static_cast<double>(sharded.throughput_ops_per_sec) /
                         static_cast<double>(global.throughput_ops_per_sec)
                  << "x\n";
    }

    std::filesystem::create_directories("results");
    std::ofstream trial_csv("results/sharding_trials.csv");
    trial_csv << "mode,trial,n_symbols,threads,throughput_ops_per_sec,"
                 "p50_latency_ns,p99_latency_ns\n";
    for (const auto& result : trials) {
        trial_csv << result.mode << ',' << result.trial << ',' << result.n_symbols << ','
                  << result.threads << ',' << result.throughput_ops_per_sec << ','
                  << result.p50_latency_ns << ',' << result.p99_latency_ns << '\n';
    }

    std::ofstream summary_csv("results/sharding_results.csv");
    summary_csv << "mode,n_trials,n_symbols,threads,throughput_ops_per_sec,"
                   "p50_latency_ns,p99_latency_ns\n";
    for (const auto& result : summaries) {
        summary_csv << result.mode << ',' << N_TRIALS << ',' << result.n_symbols << ','
                    << result.threads << ',' << result.throughput_ops_per_sec << ','
                    << result.p50_latency_ns << ',' << result.p99_latency_ns << '\n';
    }

    std::cout << "Results saved to results/sharding_results.csv and "
                 "results/sharding_trials.csv\n";
}
