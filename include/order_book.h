#pragma once
#include "order.h"
#include "lock_policy.h"
#include <map>
#include <unordered_map>
#include <optional>
#include <list>
#include <atomic>

template <typename LockPolicy = SharedMutexPolicy>
class OrderBook {
public:
    OrderBook() = default;

    AddResult add_order(const Order& order);
    bool cancel_order(uint64_t order_id);

    std::optional<uint64_t> best_bid_price() const;
    std::optional<uint64_t> best_ask_price() const;

    size_t total_orders() const;
    size_t total_bid_levels() const;
    size_t total_ask_levels() const;

private:
    mutable typename LockPolicy::mutex_type mtx_;

    std::map<uint64_t, std::list<Order>> bids_;
    std::map<uint64_t, std::list<Order>> asks_;
    std::unordered_map<uint64_t, Order*> orders_;

    static std::atomic<uint64_t> next_trade_id_;

    void add_limit_order(const Order& order, std::vector<Trade>& trades);
    void match_market_order(Order& order, std::vector<Trade>& trades);
    void execute_trade(Order& incoming, Order& resting, uint64_t exec_qty, std::vector<Trade>& trades);
};

using ExclusiveOrderBook = OrderBook<MutexPolicy>;
using SharedOrderBook    = OrderBook<SharedMutexPolicy>;
