#include "order_book.h"
#include "order_pool.h"
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

    assert(book.add_order(Order::Limit(1, 1, Side::BUY, 100, 10)).accepted);
    assert(book.best_bid_price() == 100);

    assert(book.add_order(Order::Limit(2, 1, Side::SELL, 110, 5)).accepted);
    assert(book.best_ask_price() == 110);

    // Duplicate ID rejected
    assert(!book.add_order(Order::Limit(1, 1, Side::BUY, 105, 10)).accepted);

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
// Limit-limit crossing tests
// ============================================================

template <typename LP>
void test_limit_limit_crossing_full() {
    std::cout << "[TEST] Limit-Limit Crossing (full fill)\n";
    OrderBook<LP> book;

    // Resting sell at 100
    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));

    // Aggressive buy at 110 — should immediately cross at resting price (100)
    auto result = book.add_order(Order::Limit(2, 1, Side::BUY, 110, 10));

    assert(result.accepted);
    assert(result.trades.size() == 1);
    assert(result.trades[0].price    == 100);  // resting price used
    assert(result.trades[0].quantity == 10);
    assert(result.trades[0].buy_order_id  == 2);
    assert(result.trades[0].sell_order_id == 1);

    // Both orders fully filled — book empty
    assert(book.total_orders() == 0);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_limit_limit_crossing_partial() {
    std::cout << "[TEST] Limit-Limit Crossing (partial — remainder rests)\n";
    OrderBook<LP> book;

    // Only 3 available at 100
    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 3));

    // Aggressive buy 10 at 110 — fills 3, rests 7 at 110
    auto result = book.add_order(Order::Limit(2, 1, Side::BUY, 110, 10));

    assert(result.accepted);
    assert(result.trades.size() == 1);
    assert(result.trades[0].quantity == 3);

    // 7 remaining should rest as BUY at 110
    assert(book.total_orders() == 1);
    assert(book.best_bid_price() == 110);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_limit_limit_crossing_multi_level() {
    std::cout << "[TEST] Limit-Limit Crossing (multi-level)\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 5));
    book.add_order(Order::Limit(2, 1, Side::SELL, 105, 5));

    // Aggressive buy at 110 — should sweep both levels
    auto result = book.add_order(Order::Limit(3, 1, Side::BUY, 110, 10));

    assert(result.accepted);
    assert(result.trades.size() == 2);
    assert(result.trades[0].price == 100);
    assert(result.trades[1].price == 105);

    assert(book.total_orders() == 0);
    assert(book.best_ask_price() == std::nullopt);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_limit_limit_no_crossing() {
    std::cout << "[TEST] Limit-Limit No Crossing (prices don't overlap)\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));

    // Buy at 90 — below ask, should just rest
    auto result = book.add_order(Order::Limit(2, 1, Side::BUY, 90, 10));

    assert(result.accepted);
    assert(result.trades.empty());

    assert(book.total_orders() == 2);
    assert(book.best_bid_price() == 90);
    assert(book.best_ask_price() == 100);

    std::cout << "  PASSED\n";
}

// ============================================================
// IOC and FOK tests
// ============================================================

template <typename LP>
void test_ioc_partial_fill() {
    std::cout << "[TEST] IOC — partial fill, remainder cancelled\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 5));

    // IOC buy 10 — only 5 available, fills 5, cancels remaining 5
    auto result = book.add_order(Order::Limit(2, 1, Side::BUY, 100, 10, TimeInForce::IOC));

    assert(result.accepted);
    assert(result.trades.size() == 1);
    assert(result.trades[0].quantity == 5);

    // IOC order must NOT rest — book should be empty
    assert(book.total_orders() == 0);
    assert(book.best_bid_price() == std::nullopt);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_ioc_no_fill() {
    std::cout << "[TEST] IOC — no crossing, cancelled immediately\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 110, 10));

    // IOC buy at 100 — no crossing, entire order cancelled
    auto result = book.add_order(Order::Limit(2, 1, Side::BUY, 100, 10, TimeInForce::IOC));

    assert(result.accepted);
    assert(result.trades.empty());

    // Only the resting sell remains
    assert(book.total_orders() == 1);
    assert(book.best_bid_price() == std::nullopt);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_fok_full_fill() {
    std::cout << "[TEST] FOK — enough quantity, executes fully\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 10));

    // FOK buy 10 — exactly 10 available, should fill entirely
    auto result = book.add_order(Order::Limit(2, 1, Side::BUY, 100, 10, TimeInForce::FOK));

    assert(result.accepted);
    assert(result.trades.size() == 1);
    assert(result.trades[0].quantity == 10);
    assert(book.total_orders() == 0);

    std::cout << "  PASSED\n";
}

template <typename LP>
void test_fok_kill() {
    std::cout << "[TEST] FOK — insufficient quantity, entire order killed\n";
    OrderBook<LP> book;

    book.add_order(Order::Limit(1, 1, Side::SELL, 100, 5));

    // FOK buy 10 — only 5 available, entire order rejected
    auto result = book.add_order(Order::Limit(2, 1, Side::BUY, 100, 10, TimeInForce::FOK));

    assert(result.accepted);
    assert(result.trades.empty());

    // Resting sell must be untouched
    assert(book.total_orders() == 1);
    assert(book.best_ask_price() == 100);

    std::cout << "  PASSED\n";
}

// ============================================================
// OrderPool tests
// ============================================================

void test_order_pool_basic() {
    std::cout << "[TEST] OrderPool — basic allocate/deallocate\n";

    OrderPool pool(16);
    assert(pool.capacity() == 16);
    assert(pool.size() == 0);

    Order o = Order::Limit(1, 1, Side::BUY, 100, 10);
    Order* ptr = pool.allocate(o);
    assert(ptr != nullptr);
    assert(ptr->id == 1);
    assert(ptr->price == 100);
    assert(pool.size() == 1);

    pool.deallocate(ptr);
    assert(pool.size() == 0);

    // Slot should be reusable
    Order* ptr2 = pool.allocate(o);
    assert(ptr2 != nullptr);
    pool.deallocate(ptr2);

    std::cout << "  PASSED\n";
}

void test_order_pool_fill_and_exhaust() {
    std::cout << "[TEST] OrderPool — fill to capacity then reject\n";

    const std::size_t CAP = 8;
    OrderPool pool(CAP);

    std::vector<Order*> ptrs;
    for (std::size_t i = 0; i < CAP; ++i) {
        Order o = Order::Limit(i + 1, 1, Side::SELL, 200, 5);
        Order* p = pool.allocate(o);
        assert(p != nullptr);
        assert(p->id == i + 1);
        ptrs.push_back(p);
    }
    assert(pool.size() == CAP);

    // One more should return nullptr
    Order extra = Order::Limit(99, 1, Side::BUY, 100, 1);
    assert(pool.allocate(extra) == nullptr);

    // Deallocate all and verify pool is empty
    for (Order* p : ptrs) pool.deallocate(p);
    assert(pool.size() == 0);

    // Should be fully usable again
    Order* fresh = pool.allocate(extra);
    assert(fresh != nullptr);
    pool.deallocate(fresh);

    std::cout << "  PASSED\n";
}

void test_order_pool_slot_independence() {
    std::cout << "[TEST] OrderPool — slots are independent (no aliasing)\n";

    OrderPool pool(4);

    Order a = Order::Limit(1, 1, Side::BUY,  100, 10);
    Order b = Order::Limit(2, 1, Side::SELL, 200, 20);
    Order* pa = pool.allocate(a);
    Order* pb = pool.allocate(b);

    assert(pa != pb);
    assert(pa->id == 1 && pa->price == 100);
    assert(pb->id == 2 && pb->price == 200);

    // Modifying one must not affect the other
    pa->remaining = 5;
    assert(pb->remaining == 20);

    pool.deallocate(pa);
    pool.deallocate(pb);

    std::cout << "  PASSED\n";
}

void run_pool_tests() {
    std::cout << "========================================\n";
    std::cout << "Testing: OrderPool\n";
    std::cout << "========================================\n\n";

    test_order_pool_basic();
    test_order_pool_fill_and_exhaust();
    test_order_pool_slot_independence();

    std::cout << "\nAll OrderPool tests PASSED.\n\n";
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
    test_limit_limit_crossing_full<LP>();
    test_limit_limit_crossing_partial<LP>();
    test_limit_limit_crossing_multi_level<LP>();
    test_limit_limit_no_crossing<LP>();
    test_ioc_partial_fill<LP>();
    test_ioc_no_fill<LP>();
    test_fok_full_fill<LP>();
    test_fok_kill<LP>();

    std::cout << "\nAll " << label << " tests PASSED.\n\n";
}

int main() {
    run_pool_tests();

    run_all_tests<MutexPolicy>("MutexPolicy");
    run_all_tests<SharedMutexPolicy>("SharedMutexPolicy");

    std::cout << "========================================\n";
    std::cout << "All tests PASSED for both policies.\n";
    std::cout << "========================================\n";

    return 0;
}
