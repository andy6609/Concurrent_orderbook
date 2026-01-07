#pragma once
#include "order.h"
#include <map>
#include <unordered_map>
#include <optional>
#include <list>
#include <shared_mutex>

// ============================================================================
// Step 3 학습 포인트 1: "Read-Write Lock 전략"
// ============================================================================
// Step 2의 문제: 모든 연산이 하나의 mutex 대기 (exclusive)
// 
// Step 3의 해결책: Read-Write Lock (shared_mutex)
//   - 읽기(read): 여러 스레드가 동시에 가능 (shared mode)
//   - 쓰기(write): 배타적 (exclusive mode)
//
// ⚠️ 중요: 이것은 "fine-grained lock"이 아닙니다!
//   - Fine-grained: Lock을 여러 개로 쪼개기 (예: symbol별, price level별)
//   - Read-Write Lock: 하나의 lock을 모드로 분리 (read/write)
//
// 왜 이게 도움이 되나?
//   - best_bid_price() 같은 조회는 동시에 여러 개 가능
//   - add_order()만 배타적
//   - 읽기가 많은 워크로드에서 큰 개선
// ============================================================================

class FineGrainedOrderBook {
public:
    FineGrainedOrderBook() = default;
    
    // ========================================================================
    // Step 3 학습 포인트 2: "Lock 선택의 트레이드오프"
    // ========================================================================
    // 옵션 1: Symbol별 mutex
    //   - 장점: 다른 심볼은 완전 독립적
    //   - 단점: 심볼이 하나면 의미 없음
    //
    // 옵션 2: Read-Write lock (선택!)
    //   - 장점: 구현 간단, 읽기 많은 경우 효과적
    //   - 단점: 쓰기가 많으면 Step 2와 비슷
    //
    // 옵션 3: Price level별 lock
    //   - 장점: 최대 병렬성
    //   - 단점: 구현 복잡, 데드락 위험
    //
    // CV용으로는 옵션 2가 최적:
    //   - 설명 가능
    //   - 측정 가능한 개선
    //   - 복잡도 적정
    // ========================================================================
    
    bool add_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    
    std::optional<uint64_t> best_bid_price() const;
    std::optional<uint64_t> best_ask_price() const;
    
    size_t total_orders() const;
    
private:
    // ========================================================================
    // Step 3 학습 포인트 3: "shared_mutex의 두 가지 lock"
    // ========================================================================
    // 1. shared_lock: 읽기 (여러 스레드 동시 가능)
    // 2. unique_lock: 쓰기 (배타적)
    //
    // 내부 동작:
    //   - reader count 관리
    //   - writer가 대기 중이면 새 reader도 대기
    //   - writer가 없으면 reader들 동시 진행
    // ========================================================================
    mutable std::shared_mutex mutex_;
    
    std::map<uint64_t, std::list<Order>> bids_;
    std::map<uint64_t, std::list<Order>> asks_;
    std::unordered_map<uint64_t, Order*> orders_;
    
    void add_limit_order(const Order& order);
    void match_market_order(Order order);
    void execute_trade(Order& incoming, Order& resting, uint64_t exec_qty);
};

