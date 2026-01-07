#pragma once
#include <cstdint>
#include <iostream>

// ============================================================================
// Step 1 학습 포인트 1: "주문이란 무엇인가?"
// ============================================================================
// Order Book의 가장 기본 단위
// 실제 거래소에서 사용자가 제출하는 주문을 추상화
//
// 질문: 왜 이 필드들만?
// 답변: CV용 프로젝트는 "핵심 개념 증명"이 목표
//      Stop order, IOC 같은 건 complexity만 증가
// ============================================================================

enum class Side {
    BUY,   // 매수 주문
    SELL   // 매도 주문
};

enum class OrderType {
    LIMIT,   // 지정가: 특정 가격 이하/이상에서만 체결
    MARKET   // 시장가: 현재 최선 가격에 즉시 체결
};

struct Order {
    uint64_t id;           // 주문 고유 번호 (중복 불가)
    uint32_t symbol_id;    // 어떤 종목? (예: AAPL=1, TSLA=2)
    
    OrderType type;
    Side side;
    
    uint64_t price;        // 가격 (cents 단위로 저장, $100.50 = 10050)
                          // ⚠️ 주의: MARKET 주문은 price=0
    
    uint64_t quantity;     // 원래 주문 수량
    uint64_t remaining;    // 아직 체결 안 된 수량
    
    // ========================================================================
    // Step 1 학습 포인트 2: "생성자 패턴"
    // ========================================================================
    // 질문: 왜 여러 생성자?
    // 답변: 실수 방지 + 가독성
    //      Order::Limit(...)가 Order(id, LIMIT, ...)보다 명확
    // ========================================================================
    
    static Order Limit(uint64_t id, uint32_t symbol, Side side, 
                       uint64_t price, uint64_t qty) {
        return Order{id, symbol, OrderType::LIMIT, side, price, qty, qty};
    }
    
    static Order Market(uint64_t id, uint32_t symbol, Side side, uint64_t qty) {
        return Order{id, symbol, OrderType::MARKET, side, 0, qty, qty};
    }
    
    bool is_filled() const { return remaining == 0; }
};

// ============================================================================
// Step 1 학습 포인트 3: "디버깅 편의"
// ============================================================================
// operator<< 오버로딩 → 주문을 쉽게 출력 가능
// 나중에 테스트할 때: std::cout << order; 로 바로 확인
// ============================================================================
inline std::ostream& operator<<(std::ostream& os, const Order& o) {
    os << "Order{id=" << o.id 
       << " " << (o.side == Side::BUY ? "BUY" : "SELL")
       << " " << (o.type == OrderType::LIMIT ? "LIMIT" : "MARKET")
       << " price=" << o.price << " qty=" << o.remaining << "}";
    return os;
}

