#include "order_book.h"
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
#include <atomic>
#include <random>

std::atomic<uint64_t> total_ops{0};
std::atomic<uint64_t> next_order_id{1};

void worker_thread(OrderBook* book, int num_ops, int thread_id) {
    std::mt19937 rng(thread_id);
    std::uniform_int_distribution<uint64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> op_dist(0, 99);
    
    for (int i = 0; i < num_ops; ++i) {
        int op_type = op_dist(rng);
        
        if (op_type < 70) {
            // Read operations (70%)
            book->best_bid_price();
            book->best_ask_price();
        } else {
            // Write operations (30%)
            uint64_t id = next_order_id.fetch_add(1);
            Side side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
            uint64_t price = price_dist(rng);
            uint64_t qty = qty_dist(rng);
            
            Order order = Order::Limit(id, 1, side, price, qty);
            book->add_order(order);
        }
        
        total_ops.fetch_add(1);
    }
}

void benchmark(int num_threads, int ops_per_thread) {
    OrderBook book;
    std::vector<std::thread> threads;
    
    total_ops = 0;
    next_order_id = 1;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread, &book, ops_per_thread, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    uint64_t total = total_ops.load();
    double seconds = duration.count() / 1000.0;
    double throughput = total / seconds;
    double avg_latency_ns = (duration.count() * 1e6) / total;
    
    std::cout << "Threads: " << num_threads 
              << " | Ops: " << total
              << " | Time: " << duration.count() << "ms"
              << " | Throughput: " << static_cast<int>(throughput) << " ops/sec"
              << " | Avg latency: " << static_cast<int>(avg_latency_ns) << " ns"
              << std::endl;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Performance Benchmark\n";
    std::cout << "========================================\n\n";
    
    const int OPS_PER_THREAD = 100000;
    
    std::cout << "Workload: 70% read / 30% write\n";
    std::cout << "Operations per thread: " << OPS_PER_THREAD << "\n\n";
    
    benchmark(1, OPS_PER_THREAD);
    benchmark(2, OPS_PER_THREAD);
    benchmark(4, OPS_PER_THREAD);
    benchmark(8, OPS_PER_THREAD);
    
    std::cout << "\n========================================\n";
    
    return 0;
}

