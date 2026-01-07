#include "order_book.h"
#include <iostream>
#include <cassert>

// ============================================================================
// Step 1 학습 포인트 14: "테스트의 목적"
// ============================================================================
// 1. Correctness 증명 (가장 중요!)
// 2. 회귀 방지 (나중에 Step 2, 3에서 로직 안 깨졌는지 확인)
// 3. 면접 대비 (동작하는 코드를 보여줄 수 있음)
// ============================================================================

void test_add_limit_order() {
    std::cout << "[TEST] Add Limit Order\n";
    OrderBook book;
    
    // BUY 주문 추가
    Order o1 = Order::Limit(1, 1, Side::BUY, 100, 10);
    assert(book.add_order(o1));
    assert(book.best_bid_price() == 100);
    
    // SELL 주문 추가
    Order o2 = Order::Limit(2, 1, Side::SELL, 110, 5);
    assert(book.add_order(o2));
    assert(book.best_ask_price() == 110);
    
    std::cout << "  ✓ Best bid: " << book.best_bid_price().value() << "\n";
    std::cout << "  ✓ Best ask: " << book.best_ask_price().value() << "\n";
    std::cout << "  PASSED\n\n";
}

void test_price_time_priority() {
    std::cout << "[TEST] Price-Time Priority\n";
    OrderBook book;
    
    // 같은 가격에 여러 주문
    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::SELL, 100, 5));
    book.add_order(Order::Limit(3, 1, Side::SELL, 100, 3));
    
    // Market order로 매칭
    // → 시간 순서대로 체결되어야 함 (1 → 2 → 3)
    Order market = Order::Market(100, 1, Side::BUY, 12);
    book.add_order(market);
    
    // 주문 1은 완전 체결 (10)
    // 주문 2는 부분 체결 (2/5)
    // 주문 3은 미체결
    assert(book.total_orders() == 2);  // 2와 3만 남음
    
    std::cout << "  ✓ Time priority maintained\n";
    std::cout << "  PASSED\n\n";
}

void test_cancel_order() {
    std::cout << "[TEST] Cancel Order\n";
    OrderBook book;
    
    book.add_order(Order::Limit(1, 1, Side::BUY, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::BUY, 100, 5));
    
    assert(book.total_orders() == 2);
    
    // 취소
    assert(book.cancel_order(1) == true);
    assert(book.total_orders() == 1);
    
    // 중복 취소
    assert(book.cancel_order(1) == false);
    
    std::cout << "  ✓ Cancel works correctly\n";
    std::cout << "  PASSED\n\n";
}

void test_market_order_matching() {
    std::cout << "[TEST] Market Order Matching\n";
    OrderBook book;
    
    // Limit orders
    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::SELL, 101, 10));
    book.add_order(Order::Limit(3, 1, Side::SELL, 102, 10));
    
    // Market BUY (15개)
    // → 100에서 10개, 101에서 5개 체결
    Order market = Order::Market(100, 1, Side::BUY, 15);
    book.add_order(market);
    
    assert(book.best_ask_price() == 101);  // 100은 소진
    assert(book.total_orders() == 2);      // 2와 3만 남음
    
    std::cout << "  ✓ Market order matched correctly\n";
    std::cout << "  PASSED\n\n";
}

void test_duplicate_order_id() {
    std::cout << "[TEST] Duplicate Order ID\n";
    OrderBook book;
    
    book.add_order(Order::Limit(1, 1, Side::BUY, 100, 10));
    
    // 같은 ID로 다시 추가 시도
    bool result = book.add_order(Order::Limit(1, 1, Side::SELL, 110, 5));
    assert(result == false);
    
    std::cout << "  ✓ Duplicate ID rejected\n";
    std::cout << "  PASSED\n\n";
}

// ============================================================================
// Step 1 학습 포인트 15: "테스트를 실행하는 방법"
// ============================================================================
// 빌드 후:
// $ ./test_correctness
//
// 모든 assert가 통과하면 → Correctness 증명됨
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Step 1: Correctness Tests\n";
    std::cout << "========================================\n\n";
    
    test_add_limit_order();
    test_price_time_priority();
    test_cancel_order();
    test_market_order_matching();
    test_duplicate_order_id();
    
    std::cout << "========================================\n";
    std::cout << "All tests PASSED! ✓\n";
    std::cout << "========================================\n";
    
    return 0;
}

