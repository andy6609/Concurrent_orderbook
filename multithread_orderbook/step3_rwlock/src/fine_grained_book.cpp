#include "fine_grained_book.h"
#include <algorithm>

// ============================================================================
// Step 3 학습 포인트 4: "unique_lock vs shared_lock"
// ============================================================================

bool FineGrainedOrderBook::add_order(const Order& order) {
    // ========================================================================
    // Step 3 학습 포인트 5: "쓰기 연산 = unique_lock"
    // ========================================================================
    // add_order는 자료구조를 수정 → 배타적 lock 필요
    // 
    // 다른 스레드:
    //   - add_order() 대기
    //   - best_bid_price() 대기
    //
    // Step 2와 동일? 아니요!
    //   - 읽기끼리는 동시 가능 (Step 2에서는 불가능)
    // ========================================================================
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
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

bool FineGrainedOrderBook::cancel_order(uint64_t order_id) {
    // 쓰기 연산 → unique_lock
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
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
// Step 3 학습 포인트 6: "읽기 연산 = shared_lock"
// ============================================================================
// 핵심 개선 포인트!
//
// Step 2:
//   Thread A: best_bid_price() [lock]
//   Thread B: best_ask_price() [wait...]
//
// Step 3:
//   Thread A: best_bid_price() [shared_lock]
//   Thread B: best_ask_price() [shared_lock] ← 동시 진행!
// ============================================================================

std::optional<uint64_t> FineGrainedOrderBook::best_bid_price() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // ← 여기가 변화!
    if (bids_.empty()) return std::nullopt;
    return bids_.rbegin()->first;
}

std::optional<uint64_t> FineGrainedOrderBook::best_ask_price() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // ← 여기가 변화!
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

size_t FineGrainedOrderBook::total_orders() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return orders_.size();
}

// ============================================================================
// 아래는 Step 1, 2와 동일
// ============================================================================

void FineGrainedOrderBook::add_limit_order(const Order& order) {
    auto& levels = (order.side == Side::BUY) ? bids_ : asks_;
    auto& level_orders = levels[order.price];
    level_orders.push_back(order);
    orders_[order.id] = &level_orders.back();
}

void FineGrainedOrderBook::match_market_order(Order order) {
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

void FineGrainedOrderBook::execute_trade(Order& incoming, Order& resting, uint64_t qty) {
    incoming.remaining -= qty;
    resting.remaining -= qty;
    
    if (resting.is_filled()) {
        orders_.erase(resting.id);
    }
}

