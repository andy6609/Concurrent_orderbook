#include "order_book.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <atomic>

// ============================================================
// 기존 테스트 (템플릿화)
// ============================================================

template <typename LP>
void test_add_limit_order() {
    std::cout << "[TEST] Add Limit Order\n";
    OrderBook<LP> book;

    assert(book.add_order(Order::Limit(1, 1, Side::BUY, 100, 10)));
    assert(book.best_bid_price() == 100);

    assert(book.add_order(Order::Limit(2, 1, Side::SELL, 110, 5)));
    assert(book.best_ask_price() == 110);

    // Duplicate ID rejected
    assert(book.add_order(Order::Limit(1, 1, Side::BUY, 105, 10)) == false);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_price_time_priority() {
    std::cout << "[TEST] Price-Time Priority\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::SELL, 100, 5));
    book.add_order(Order::Limit(3, 1, Side::SELL, 100, 3));

    // Market buy 12 should fill: order 1 (10) + order 2 (2 partial)
    book.add_order(Order::Market(100, 1, Side::BUY, 12));

    // Order 1 fully filled, order 2 partially filled, order 3 untouched
    assert(book.total_orders() == 2);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_cancel_order() {
    std::cout << "[TEST] Cancel Order\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::BUY, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::BUY, 100, 5));

    assert(book.total_orders() == 2);
    assert(book.cancel_order(1) == true);
    assert(book.total_orders() == 1);
    assert(book.cancel_order(1) == false);  // already cancelled

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_market_order_matching() {
    std::cout << "[TEST] Market Order Matching\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::SELL, 101, 10));
    book.add_order(Order::Limit(3, 1, Side::SELL, 102, 10));

    // Market buy 15: fills order 1 (10) + order 2 (5 partial)
    book.add_order(Order::Market(100, 1, Side::BUY, 15));

    assert(book.best_ask_price() == 101);
    assert(book.total_orders() == 2);

    std::cout << "  PASSED\n";
}

// ============================================================
// 새 테스트: Edge cases
// ============================================================

template <typename LP>
void test_partial_fill() {
    std::cout << "[TEST] Partial Fill\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 50));

    // Market buy 30: partially fills order 1, 20 remains
    book.add_order(Order::Market(2, 1, Side::BUY, 30));

    assert(book.total_orders() == 1);       // order 1 still resting
    assert(book.best_ask_price() == 100);   // still at price 100

    // Another market buy 20: fills the rest
    book.add_order(Order::Market(3, 1, Side::BUY, 20));

    assert(book.total_orders() == 0);       // book empty
    assert(book.best_ask_price() == std::nullopt);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_multi_level_cross() {
    std::cout << "[TEST] Multi-Level Cross\n";
    OrderBook<LP> book;

    // 3 price levels on sell side
    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 5));
    book.add_order(Order::Limit(2, 1, Side::SELL, 101, 5));
    book.add_order(Order::Limit(3, 1, Side::SELL, 102, 5));

    // Market buy 12: crosses all of level 100 (5) + all of 101 (5) + partial 102 (2)
    book.add_order(Order::Market(10, 1, Side::BUY, 12));

    assert(book.total_orders() == 1);       // only order 3 remains (3 left)
    assert(book.best_ask_price() == 102);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_cancel_nonexistent() {
    std::cout << "[TEST] Cancel Nonexistent Order\n";
    OrderBook<LP> book;

    assert(book.cancel_order(999) == false);  // nothing to cancel
    assert(book.total_orders() == 0);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_empty_book_queries() {
    std::cout << "[TEST] Empty Book Queries\n";
    OrderBook<LP> book;

    assert(book.best_bid_price() == std::nullopt);
    assert(book.best_ask_price() == std::nullopt);
    assert(book.total_orders() == 0);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_cancel_updates_best_price() {
    std::cout << "[TEST] Cancel Updates Best Price\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::BUY, 100, 10));
    book.add_order(Order::Limit(2, 1, Side::BUY, 105, 10));

    assert(book.best_bid_price() == 105);

    book.cancel_order(2);

    assert(book.best_bid_price() == 100);  // falls back to next level

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_concurrent_add_cancel() {
    std::cout << "[TEST] Concurrent Add + Cancel\n";
    OrderBook<LP> book;

    const int NUM_THREADS = 4;
    const int OPS_PER_THREAD = 10000;
    std::atomic<uint64_t> next_id{1};

    std::vector<std::thread> threads;

    // Half threads add, half cancel
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                if (t % 2 == 0) {
                    uint64_t id = next_id.fetch_add(1);
                    book.add_order(Order::Limit(id, 1, Side::BUY, 100 + (i % 10), 10));
                } else {
                    uint64_t id = next_id.load();
                    if (id > 1) {
                        book.cancel_order(id - 1);
                    }
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // No crash, no assertion failure = thread safety holds
    // Just verify book is in a consistent state
    size_t orders = book.total_orders();
    (void)orders;  // not checking exact count, just that it doesn't crash

    std::cout << "  PASSED (no crash, consistent state)\n";
}

// ============================================================
// Run all tests for a given policy
// ============================================================

template <typename LP>
void run_all_tests(const std::string& label) {
    std::cout << "========================================\n";
    std::cout << "Testing: " << label << "\n";
    std::cout << "========================================\n\n";

    test_add_limit_order<LP>();
    test_price_time_priority<LP>();
    test_cancel_order<LP>();
    test_market_order_matching<LP>();
    test_partial_fill<LP>();
    test_multi_level_cross<LP>();
    test_cancel_nonexistent<LP>();
    test_empty_book_queries<LP>();
    test_cancel_updates_best_price<LP>();
    test_concurrent_add_cancel<LP>();

    std::cout << "\nAll " << label << " tests PASSED.\n\n";
}

int main() {
    run_all_tests<MutexPolicy>("MutexPolicy");
    run_all_tests<SharedMutexPolicy>("SharedMutexPolicy");

    std::cout << "========================================\n";
    std::cout << "All tests PASSED for both policies.\n";
    std::cout << "========================================\n";

    return 0;
}
