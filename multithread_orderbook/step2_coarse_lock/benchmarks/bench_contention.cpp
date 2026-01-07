#include "thread_safe_book.h"
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
#include <atomic>
#include <random>

// ============================================================================
// Step 2 학습 포인트 7: "성능 측정의 중요성"
// ============================================================================
// "느리다"는 주관적
// 숫자로 증명해야 함:
//   - Throughput (ops/sec)
//   - Latency (ns/op)
//   - Scalability (스레드 증가 시 성능)
// ============================================================================

std::atomic<uint64_t> total_ops{0};
std::atomic<uint64_t> next_order_id{1};

void worker_thread(ThreadSafeOrderBook* book, int num_ops, int thread_id) {
    // 각 스레드가 무작위 주문 제출
    std::mt19937 rng(thread_id);
    std::uniform_int_distribution<uint64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    
    for (int i = 0; i < num_ops; ++i) {
        uint64_t id = next_order_id.fetch_add(1);
        Side side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
        uint64_t price = price_dist(rng);
        uint64_t qty = qty_dist(rng);
        
        Order order = Order::Limit(id, 1, side, price, qty);
        book->add_order(order);
        
        total_ops.fetch_add(1);
    }
}

// ============================================================================
// Step 2 학습 포인트 8: "Lock Contention 측정"
// ============================================================================
// Coarse-grained lock의 문제:
//   - 스레드가 늘어도 throughput이 거의 안 늘어남
//   - CPU 사용률이 낮음 (대부분 lock 대기)
// ============================================================================

void benchmark(int num_threads, int ops_per_thread) {
    ThreadSafeOrderBook book;
    std::vector<std::thread> threads;
    
    total_ops = 0;
    next_order_id = 1;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 스레드 시작
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread, &book, ops_per_thread, i);
    }
    
    // 완료 대기
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
              << " | Total ops: " << total
              << " | Time: " << duration.count() << "ms"
              << " | Throughput: " << static_cast<int>(throughput) << " ops/sec"
              << " | Avg latency: " << static_cast<int>(avg_latency_ns) << " ns"
              << std::endl;
}

// ============================================================================
// Step 2 학습 포인트 9: "Scalability 테스트"
// ============================================================================
// 이상적: 스레드 2배 → throughput 2배
// 현실 (coarse lock): 스레드 2배 → throughput 거의 동일
//
// 왜? 
//   → 모든 스레드가 같은 mutex 대기
//   → Serialization (직렬화)
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Step 2: Coarse-Grained Lock Benchmark\n";
    std::cout << "========================================\n\n";
    
    const int OPS_PER_THREAD = 50000;
    
    std::cout << "Running benchmark (" << OPS_PER_THREAD << " ops/thread)...\n\n";
    
    benchmark(1, OPS_PER_THREAD);
    benchmark(2, OPS_PER_THREAD);
    benchmark(4, OPS_PER_THREAD);
    benchmark(8, OPS_PER_THREAD);
    
    std::cout << "\n========================================\n";
    std::cout << "분석:\n";
    std::cout << "- 스레드 증가 시 throughput이 선형 증가하지 않음\n";
    std::cout << "- Lock contention이 병목\n";
    std::cout << "- Step 3에서 read-write lock으로 개선\n";
    std::cout << "========================================\n";
    
    return 0;
}

