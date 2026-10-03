#include "order_book.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

struct WorkloadConfig {
    const char* name;
    int read_pct;
};

static constexpr WorkloadConfig WORKLOADS[] = {
    {"read_heavy", 90},
    {"balanced", 50},
    {"write_heavy", 20},
};

static constexpr int THREAD_COUNTS[] = {1, 2, 4, 8};
static constexpr int OPS_PER_THREAD = 50'000;
static constexpr int N_TRIALS = 5;

struct Operation {
    bool read;
    uint64_t id;
    Side side;
    uint64_t price;
    uint64_t quantity;
};

struct BenchResult {
    std::string workload;
    std::string policy;
    int trial;
    int threads;
    uint64_t total_ops;
    uint64_t throughput_ops_per_sec;
    uint64_t avg_latency_ns;
    uint64_t p99_latency_ns;
};

std::vector<std::vector<Operation>> make_operations(int thread_count,
                                                    int read_pct) {
    std::vector<std::vector<Operation>> operations(thread_count);
    for (int thread = 0; thread < thread_count; ++thread) {
        auto& thread_ops = operations[thread];
        thread_ops.reserve(OPS_PER_THREAD);

        std::mt19937 rng(static_cast<uint32_t>(thread) * 1'234'567u + 42u);
        std::uniform_int_distribution<uint64_t> price_dist(9'900, 10'100);
        std::uniform_int_distribution<uint64_t> quantity_dist(1, 100);
        std::uniform_int_distribution<int> side_dist(0, 1);
        std::uniform_int_distribution<int> operation_dist(0, 99);

        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            const uint64_t id = static_cast<uint64_t>(thread) * OPS_PER_THREAD + i + 1;
            thread_ops.push_back(Operation{
                operation_dist(rng) < read_pct,
                id,
                side_dist(rng) == 0 ? Side::BUY : Side::SELL,
                price_dist(rng),
                quantity_dist(rng),
            });
        }
    }
    return operations;
}

template <typename LockPolicy>
void execute_operations(OrderBook<LockPolicy>& book,
                        const std::vector<Operation>& operations,
                        std::vector<uint64_t>* latency_samples,
                        std::atomic<int>& failures) {
    if (latency_samples != nullptr) latency_samples->reserve(operations.size());

    for (const auto& operation : operations) {
        const auto start = latency_samples != nullptr ? Clock::now() : Clock::time_point{};

        if (operation.read) {
            book.best_bid_price();
            book.best_ask_price();
        } else {
            const auto result = book.add_order(Order::Limit(
                operation.id, 1, operation.side, operation.price, operation.quantity));
            if (!result.accepted)
                failures.fetch_add(1, std::memory_order_relaxed);
        }

        if (latency_samples != nullptr) {
            const auto end = Clock::now();
            latency_samples->push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
    }
}

template <typename LockPolicy>
uint64_t run_throughput(
    const std::vector<std::vector<Operation>>& operations,
    std::size_t pool_capacity) {
    OrderBook<LockPolicy> book(pool_capacity);
    std::vector<std::thread> workers;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> failures{0};

    for (std::size_t thread = 0; thread < operations.size(); ++thread) {
        workers.emplace_back([&book, &operations, thread, &ready, &start, &failures]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            execute_operations(book, operations[thread], nullptr, failures);
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

    const uint64_t total_ops =
        static_cast<uint64_t>(operations.size()) * OPS_PER_THREAD;
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        wall_end - wall_start).count();
    return static_cast<uint64_t>(
        static_cast<long double>(total_ops) * 1'000'000'000.0L /
        static_cast<long double>(elapsed_ns));
}

template <typename LockPolicy>
std::pair<uint64_t, uint64_t> run_latency(
    const std::vector<std::vector<Operation>>& operations,
    std::size_t pool_capacity) {
    OrderBook<LockPolicy> book(pool_capacity);
    std::vector<std::thread> workers;
    std::vector<std::vector<uint64_t>> samples(operations.size());
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> failures{0};

    for (std::size_t thread = 0; thread < operations.size(); ++thread) {
        workers.emplace_back([&, thread]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            execute_operations(book, operations[thread], &samples[thread], failures);
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

    const uint64_t average = std::accumulate(flat.begin(), flat.end(), uint64_t{0}) /
                             static_cast<uint64_t>(flat.size());
    const uint64_t p99 = flat[(flat.size() - 1) * 99 / 100];
    return {average, p99};
}

template <typename LockPolicy>
BenchResult run_one(const WorkloadConfig& workload,
                    int thread_count,
                    int trial,
                    const char* policy_name,
                    const std::vector<std::vector<Operation>>& operations) {
    const std::size_t pool_capacity =
        static_cast<std::size_t>(thread_count) * OPS_PER_THREAD + 1'000;
    const uint64_t throughput = run_throughput<LockPolicy>(operations, pool_capacity);
    const auto [average, p99] = run_latency<LockPolicy>(operations, pool_capacity);

    return BenchResult{
        workload.name,
        policy_name,
        trial,
        thread_count,
        static_cast<uint64_t>(thread_count) * OPS_PER_THREAD,
        throughput,
        average,
        p99,
    };
}

uint64_t median(std::vector<uint64_t> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

BenchResult summarize(const WorkloadConfig& workload,
                      int thread_count,
                      const std::string& policy,
                      const std::vector<BenchResult>& trials) {
    std::vector<uint64_t> throughput;
    std::vector<uint64_t> average;
    std::vector<uint64_t> p99;
    for (const auto& result : trials) {
        if (result.workload != workload.name || result.threads != thread_count ||
            result.policy != policy)
            continue;
        throughput.push_back(result.throughput_ops_per_sec);
        average.push_back(result.avg_latency_ns);
        p99.push_back(result.p99_latency_ns);
    }

    return BenchResult{
        workload.name,
        policy,
        0,
        thread_count,
        static_cast<uint64_t>(thread_count) * OPS_PER_THREAD,
        median(throughput),
        median(average),
        median(p99),
    };
}

int main() {
    std::cout << "Controlled benchmark: mutex vs shared_mutex\n"
              << "Inputs generated outside timing; throughput and latency use separate books\n"
              << "ops_per_thread=" << OPS_PER_THREAD << ", trials=" << N_TRIALS
              << " (reported values are medians)\n\n";

    std::vector<BenchResult> trials;
    std::vector<BenchResult> summaries;
    trials.reserve(24 * N_TRIALS);
    summaries.reserve(24);

    for (const auto& workload : WORKLOADS) {
        std::cout << "=== " << workload.name << " (read=" << workload.read_pct
                  << "% write=" << 100 - workload.read_pct << "%) ===\n";

        for (int thread_count : THREAD_COUNTS) {
            const auto operations = make_operations(thread_count, workload.read_pct);
            for (int trial = 1; trial <= N_TRIALS; ++trial) {
                if (trial % 2 == 1) {
                    trials.push_back(run_one<MutexPolicy>(
                        workload, thread_count, trial, "MutexPolicy", operations));
                    trials.push_back(run_one<SharedMutexPolicy>(
                        workload, thread_count, trial, "SharedMutexPolicy", operations));
                } else {
                    trials.push_back(run_one<SharedMutexPolicy>(
                        workload, thread_count, trial, "SharedMutexPolicy", operations));
                    trials.push_back(run_one<MutexPolicy>(
                        workload, thread_count, trial, "MutexPolicy", operations));
                }
            }

            const auto mutex = summarize(workload, thread_count, "MutexPolicy", trials);
            const auto shared = summarize(workload, thread_count, "SharedMutexPolicy", trials);
            summaries.push_back(mutex);
            summaries.push_back(shared);

            auto print = [](const BenchResult& result) {
                std::cout << "  [" << result.policy << "] threads=" << result.threads
                          << " | throughput=" << result.throughput_ops_per_sec << " ops/s"
                          << " | avg=" << result.avg_latency_ns << " ns"
                          << " | p99=" << result.p99_latency_ns << " ns\n";
            };
            print(mutex);
            print(shared);
        }
        std::cout << '\n';
    }

    std::filesystem::create_directories("results");
    std::ofstream trial_csv("results/benchmark_trials.csv");
    trial_csv << "workload,policy,trial,threads,total_ops,"
                 "throughput_ops_per_sec,avg_latency_ns,p99_latency_ns\n";
    for (const auto& result : trials) {
        trial_csv << result.workload << ',' << result.policy << ',' << result.trial << ','
                  << result.threads << ',' << result.total_ops << ','
                  << result.throughput_ops_per_sec << ',' << result.avg_latency_ns << ','
                  << result.p99_latency_ns << '\n';
    }

    std::ofstream summary_csv("results/benchmark_results.csv");
    summary_csv << "workload,policy,n_trials,threads,total_ops,"
                   "throughput_ops_per_sec,avg_latency_ns,p99_latency_ns\n";
    for (const auto& result : summaries) {
        summary_csv << result.workload << ',' << result.policy << ',' << N_TRIALS << ','
                    << result.threads << ',' << result.total_ops << ','
                    << result.throughput_ops_per_sec << ',' << result.avg_latency_ns << ','
                    << result.p99_latency_ns << '\n';
    }

    std::cout << "Results saved to results/benchmark_results.csv and "
                 "results/benchmark_trials.csv\n";
}
