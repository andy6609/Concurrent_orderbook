#pragma once
#include "order.h"
#include <map>
#include <unordered_map>
#include <optional>
#include <list>
#include <mutex>

// ============================================================================
// Step 2 학습 포인트 1: "Thread Safety의 시작"
// ============================================================================
// Step 1 코드는 단일 스레드에서만 동작
// 여러 스레드가 동시에 add_order()를 호출하면?
//   → Race condition
//   → Data corruption
//   → Crash
//
// 해결책: Mutex (Mutual Exclusion)
// ============================================================================

class ThreadSafeOrderBook {
public:
    ThreadSafeOrderBook() = default;
    
    // ========================================================================
    // Step 2 학습 포인트 2: "Coarse-Grained Lock 전략"
    // ========================================================================
    // 전략: OrderBook 전체를 하나의 mutex로 보호
    //
    // 장점:
    //   - 구현 간단 (5분이면 끝)
    //   - Correctness 보장 (race condition 없음)
    //   - 디버깅 쉬움
    //
    // 단점:
    //   - 병목 발생 (bottleneck)
    //   - 스레드 많아도 throughput 안 늘어남
    //   - Lock contention 심함
    //
    // → 이것이 Step 3의 동기
    // ========================================================================
    
    bool add_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    
    std::optional<uint64_t> best_bid_price() const;
    std::optional<uint64_t> best_ask_price() const;
    
    size_t total_orders() const;
    
private:
    // ========================================================================
    // Step 2 학습 포인트 3: "Mutex 배치"
    // ========================================================================
    // 질문: 왜 mutable?
    // 답변: const 함수(best_bid_price)에서도 lock 필요
    //      mutable = const 함수에서도 수정 가능
    //
    // 질문: 왜 하나만?
    // 답변: Coarse-grained = "전체를 하나로"
    //      모든 연산이 이 mutex를 획득해야 함
    // ========================================================================
    mutable std::mutex mutex_;
    
    // Step 1과 동일한 자료구조
    std::map<uint64_t, std::list<Order>> bids_;
    std::map<uint64_t, std::list<Order>> asks_;
    std::unordered_map<uint64_t, Order*> orders_;
    
    // 헬퍼 함수 (Step 1과 동일, 하지만 호출 전에 lock 필요)
    void add_limit_order(const Order& order);
    void match_market_order(Order order);
    void execute_trade(Order& incoming, Order& resting, uint64_t exec_qty);
};

