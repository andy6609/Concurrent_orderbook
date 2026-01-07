#pragma once
#include "order.h"
#include <map>
#include <unordered_map>
#include <optional>
#include <list>

// ============================================================================
// Step 1 학습 포인트 4: "Order Book의 핵심 책임"
// ============================================================================
// 1. 주문을 가격별로 정렬해서 저장
// 2. Best bid/ask를 빠르게 조회
// 3. 매칭 (market order가 오면 limit order와 체결)
//
// ⚠️ 이 버전은 thread-safe 하지 않음!
//    Step 2에서 mutex 추가
// ============================================================================

class OrderBook {
public:
    OrderBook() = default;
    
    // ========================================================================
    // Step 1 학습 포인트 5: "API 설계"
    // ========================================================================
    // 질문: 왜 bool 리턴?
    // 답변: 성공/실패를 명확히 (예: 중복 ID → false)
    //
    // 질문: Order를 const&로 받는 이유?
    // 답변: 복사 비용 절약 (하지만 내부에서는 복사본 저장)
    // ========================================================================
    
    bool add_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    
    // Best bid/ask 조회 (읽기 전용)
    std::optional<uint64_t> best_bid_price() const;
    std::optional<uint64_t> best_ask_price() const;
    
    // 통계 (테스트/벤치마크용)
    size_t total_orders() const { return orders_.size(); }
    size_t total_bid_levels() const { return bids_.size(); }
    size_t total_ask_levels() const { return asks_.size(); }
    
private:
    // ========================================================================
    // Step 1 학습 포인트 6: "자료구조 선택의 이유"
    // ========================================================================
    
    // 1. Price Level 저장: std::map
    //    - Key: 가격
    //    - Value: 그 가격의 모든 주문들
    //    
    //    왜 map? 
    //    → 자동으로 가격 정렬됨
    //    → best_bid = bids_.rbegin() (최고가)
    //    → best_ask = asks_.begin() (최저가)
    //
    //    왜 list<Order>?
    //    → 원소 주소가 안정적 (재할당 없음)
    //    → orders_에 저장된 포인터가 무효화되지 않음
    //    → 같은 가격 내에서 시간 순서(FIFO) 유지

    std::map<uint64_t, std::list<Order>> bids_;   // 매수 주문 (높은 가격 우선)
    std::map<uint64_t, std::list<Order>> asks_;   // 매도 주문 (낮은 가격 우선)
    
    // 2. Order ID 인덱스: unordered_map
    //    
    //    왜 필요?
    //    → cancel_order(123) 호출 시
    //    → 어느 가격 레벨에 있는지 모름
    //    → 전체를 순회하면 O(n) → 느림
    //    
    //    왜 unordered_map?
    //    → 조회가 O(1)
    //    → 정렬 필요 없음 (ID는 순서 무의미)
    //
    //    왜 Order*?
    //    → 실제 Order 객체는 bids_/asks_에 있음
    //    → 여기선 위치만 가리킴
    
    std::unordered_map<uint64_t, Order*> orders_;
    
    // ========================================================================
    // Step 1 학습 포인트 7: "private 헬퍼 함수"
    // ========================================================================
    // 코드 중복 방지 + 가독성
    // add_order()에서 LIMIT/MARKET 케이스 분기
    // ========================================================================
    
    void add_limit_order(const Order& order);
    void match_market_order(Order order);  // market은 즉시 체결 시도
    
    // 매칭 로직 (market order <-> limit orders)
    void execute_trade(Order& incoming, Order& resting, uint64_t exec_qty);
};

