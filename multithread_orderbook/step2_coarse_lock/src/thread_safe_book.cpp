#include "thread_safe_book.h"
#include <algorithm>

// ============================================================================
// Step 2 학습 포인트 4: "Lock 패턴"
// ============================================================================
// std::lock_guard: RAII 패턴
//   - 생성 시 자동으로 lock
//   - 소멸 시 자동으로 unlock
//   - 예외가 발생해도 안전 (unlock 보장)
//
// 왜 수동 lock/unlock 안 쓰나?
//   unlock() 깜빡하면 → 데드락
//   예외 발생 시 → unlock 안 됨
// ============================================================================

bool ThreadSafeOrderBook::add_order(const Order& order) {
    // ========================================================================
    // Step 2 학습 포인트 5: "Critical Section"
    // ========================================================================
    // 이 scope 전체가 "critical section"
    //   → 한 번에 한 스레드만 진입 가능
    //   → 다른 스레드는 대기 (blocked)
    //
    // 문제: add_order() 하나가 오래 걸리면?
    //   → 모든 다른 스레드가 대기
    //   → Throughput 떨어짐
    // ========================================================================
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (orders_.find(order.id) != orders_.end()) {
        return false;
    }
    
    if (order.type == OrderType::LIMIT) {
        add_limit_order(order);
    } else {
        match_market_order(order);
    }
    
    return true;
}

bool ThreadSafeOrderBook::cancel_order(uint64_t order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;
    }
    
    Order* order_ptr = it->second;
    auto& levels = (order_ptr->side == Side::BUY) ? bids_ : asks_;
    auto& level_orders = levels[order_ptr->price];
    
    level_orders.remove_if([order_id](const Order& o) { return o.id == order_id; });
    
    if (level_orders.empty()) {
        levels.erase(order_ptr->price);
    }
    
    orders_.erase(it);
    return true;
}

// ============================================================================
// Step 2 학습 포인트 6: "읽기 연산도 lock 필요"
// ============================================================================
// 질문: best_bid_price()는 읽기만 하는데 왜 lock?
// 답변: 다른 스레드가 bids_를 수정 중일 수 있음
//      → iterator 무효화
//      → crash
//
// 이것이 Step 3에서 개선할 포인트
// → shared_mutex (read-write lock) 사용
// ============================================================================

std::optional<uint64_t> ThreadSafeOrderBook::best_bid_price() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bids_.empty()) return std::nullopt;
    return bids_.rbegin()->first;
}

std::optional<uint64_t> ThreadSafeOrderBook::best_ask_price() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

size_t ThreadSafeOrderBook::total_orders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_.size();
}

// ============================================================================
// 아래는 Step 1과 동일 (단, 호출 전에 이미 lock 획득됨)
// ============================================================================

void ThreadSafeOrderBook::add_limit_order(const Order& order) {
    auto& levels = (order.side == Side::BUY) ? bids_ : asks_;
    auto& level_orders = levels[order.price];
    level_orders.push_back(order);
    orders_[order.id] = &level_orders.back();
}

void ThreadSafeOrderBook::match_market_order(Order order) {
    auto& levels = (order.side == Side::BUY) ? asks_ : bids_;
    
    while (order.remaining > 0 && !levels.empty()) {
        auto it = levels.begin();
        auto& level_orders = it->second;
        
        for (auto& resting : level_orders) {
            if (order.remaining == 0) break;
            if (resting.remaining == 0) continue;
            
            uint64_t exec_qty = std::min(order.remaining, resting.remaining);
            execute_trade(order, resting, exec_qty);
        }
        
        level_orders.remove_if([](const Order& o) { return o.is_filled(); });
        
        if (level_orders.empty()) {
            levels.erase(it);
        }
    }
}

void ThreadSafeOrderBook::execute_trade(Order& incoming, Order& resting, uint64_t qty) {
    incoming.remaining -= qty;
    resting.remaining -= qty;
    
    if (resting.is_filled()) {
        orders_.erase(resting.id);
    }
}

