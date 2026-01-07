#include "order_book.h"
#include <iostream>
#include <cassert>

void test_add_limit_order() {
    std::cout << "[TEST] Add Limit Order\n";
    OrderBook book;
    
    Order o1 = Order::Limit(1, 1, Side::BUY, 100, 10);
    assert(book.add_order(o1));
    assert(book.best_bid_price() == 100);
    
    Order o2 = Order::Limit(2, 1, Side::SELL, 110, 5);
    assert(book.add_order(o2));
    assert(book.best_ask_price() == 110);
    
    std::cout << "  PASSED\n\n";
}

void test_price_time_priority() {
    std::cout << "[TEST] Price-Time Priority\n";
    OrderBook book;
    
    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::SELL, 100, 5));
    book.add_order(Order::Limit(3, 1, Side::SELL, 100, 3));
    
    Order market = Order::Market(100, 1, Side::BUY, 12);
    book.add_order(market);
    
    assert(book.total_orders() == 2);
    
    std::cout << "  PASSED\n\n";
}

void test_cancel_order() {
    std::cout << "[TEST] Cancel Order\n";
    OrderBook book;
    
    book.add_order(Order::Limit(1, 1, Side::BUY, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::BUY, 100, 5));
    
    assert(book.total_orders() == 2);
    assert(book.cancel_order(1) == true);
    assert(book.total_orders() == 1);
    assert(book.cancel_order(1) == false);
    
    std::cout << "  PASSED\n\n";
}

void test_market_order_matching() {
    std::cout << "[TEST] Market Order Matching\n";
    OrderBook book;
    
    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::SELL, 101, 10));
    book.add_order(Order::Limit(3, 1, Side::SELL, 102, 10));
    
    Order market = Order::Market(100, 1, Side::BUY, 15);
    book.add_order(market);
    
    assert(book.best_ask_price() == 101);
    assert(book.total_orders() == 2);
    
    std::cout << "  PASSED\n\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Correctness Tests\n";
    std::cout << "========================================\n\n";
    
    test_add_limit_order();
    test_price_time_priority();
    test_cancel_order();
    test_market_order_matching();
    
    std::cout << "========================================\n";
    std::cout << "All tests PASSED!\n";
    std::cout << "========================================\n";
    
    return 0;
}

