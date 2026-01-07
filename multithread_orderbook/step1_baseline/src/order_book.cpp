#include "order_book.h"
#include <algorithm>
#include <list>

// ============================================================================
// Step 1 학습 포인트 8: "주문 추가의 두 갈래"
// ============================================================================

bool OrderBook::add_order(const Order& order) {
    // 중복 ID 체크
    if (orders_.find(order.id) != orders_.end()) {
        return false;  // 이미 존재
    }
    
    if (order.type == OrderType::LIMIT) {
        add_limit_order(order);
    } else {
        match_market_order(order);  // Market order는 즉시 매칭 시도
    }
    
    return true;
}

// ============================================================================
// Step 1 학습 포인트 9: "Limit Order는 Book에 '대기'"
// ============================================================================

void OrderBook::add_limit_order(const Order& order) {
    // Step 1: 적절한 side의 map에 추가
    auto& levels = (order.side == Side::BUY) ? bids_ : asks_;

    // Step 2: 해당 가격 레벨에 추가 (list는 재할당이 없어 포인터 안정)
    auto& level_orders = levels[order.price];
    level_orders.push_back(order);

    // Step 3: 인덱스 업데이트 (안전한 포인터)
    orders_[order.id] = &level_orders.back();
}

// ============================================================================
// Step 1 학습 포인트 10: "Market Order 매칭 로직"
// ============================================================================
// 가장 중요한 부분!
// 면접에서 화이트보드에 그려가며 설명할 수 있어야 함
//
// 매칭 규칙:
// 1. Market BUY → asks(매도 주문)와 매칭
// 2. Market SELL → bids(매수 주문)와 매칭
// 3. Best price부터 시작
// 4. 같은 가격 내에서는 시간 순서(FIFO)
// ============================================================================

void OrderBook::match_market_order(Order order) {
    // Market BUY → asks(매도 주문)와 매칭
    // Market SELL → bids(매수 주문)와 매칭
    auto& levels = (order.side == Side::BUY) ? asks_ : bids_;
    
    // Best price부터 순회
    while (order.remaining > 0 && !levels.empty()) {
        // 최선 가격 레벨
        auto it = levels.begin();
        auto& level_orders = it->second;
        
        // 그 가격의 주문들을 시간순으로 매칭
        for (auto& resting : level_orders) {
            if (order.remaining == 0) break;
            if (resting.remaining == 0) continue;  // 이미 체결됨
            
            // 체결 가능한 수량
            uint64_t exec_qty = std::min(order.remaining, resting.remaining);
            
            execute_trade(order, resting, exec_qty);
            
            // ================================================================
            // Step 1 학습 포인트 11: "디버깅을 위한 출력"
            // ================================================================
            // 나중에 테스트할 때 매칭 과정 확인 가능
            // Production 코드에서는 logging 프레임워크 사용
            // ================================================================
            // std::cout << "TRADE: " << exec_qty << " @ " << resting.price << "\n";
        }
        
        // 이 가격 레벨에서 체결 완료된 주문들 제거
        level_orders.remove_if([](const Order& o) { return o.is_filled(); });
        
        // 레벨이 비었으면 제거
        if (level_orders.empty()) {
            levels.erase(it);
        }
    }
    
    // Market order는 체결 안 된 부분은 취소됨 (Book에 남지 않음)
    // → 이것이 Market order의 특성
}

void OrderBook::execute_trade(Order& incoming, Order& resting, uint64_t qty) {
    incoming.remaining -= qty;
    resting.remaining -= qty;
    
    // 완전 체결된 주문은 인덱스에서 제거
    if (resting.is_filled()) {
        orders_.erase(resting.id);
    }
}

// ============================================================================
// Step 1 학습 포인트 12: "주문 취소"
// ============================================================================

bool OrderBook::cancel_order(uint64_t order_id) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;  // 존재하지 않음
    }
    
    Order* order_ptr = it->second;
    
    // Price level에서 제거
    // ⚠️ 이 부분이 비효율적임 (vector에서 선형 탐색)
    //    → Step 2, 3에서도 해결 안 함 (괜찮음, 핵심 아님)
    //    → 면접에서 "알고 있지만 우선순위 아님" 설명
    auto& levels = (order_ptr->side == Side::BUY) ? bids_ : asks_;
    auto& level_orders = levels[order_ptr->price];
    
    level_orders.remove_if([order_id](const Order& o) { return o.id == order_id; });
    
    // 레벨이 비었으면 제거
    if (level_orders.empty()) {
        levels.erase(order_ptr->price);
    }
    
    orders_.erase(it);
    return true;
}

// ============================================================================
// Step 1 학습 포인트 13: "Best price 조회"
// ============================================================================

std::optional<uint64_t> OrderBook::best_bid_price() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.rbegin()->first;  // 역순 이터레이터 = 최댓값
}

std::optional<uint64_t> OrderBook::best_ask_price() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;   // 첫 원소 = 최솟값
}

