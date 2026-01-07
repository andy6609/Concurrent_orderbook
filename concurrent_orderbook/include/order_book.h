#pragma once
#include "order.h"
#include <map>
#include <unordered_map>
#include <optional>
#include <list>
#include <shared_mutex>

/**
 * Thread-safe Order Book with price-time priority matching.
 * 
 * Uses read-write lock (shared_mutex) for efficient concurrent access.
 * Read operations can proceed concurrently, while write operations are exclusive.
 */
class OrderBook {
public:
    OrderBook() = default;
    
    // Order operations
    bool add_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    
    // Market data queries (thread-safe read)
    std::optional<uint64_t> best_bid_price() const;
    std::optional<uint64_t> best_ask_price() const;
    
    // Statistics
    size_t total_orders() const;
    size_t total_bid_levels() const;
    size_t total_ask_levels() const;
    
private:
    mutable std::shared_mutex mutex_;
    
    // Price levels: map<price, list<Order>> (stable iterators for pointer safety)
    std::map<uint64_t, std::list<Order>> bids_;
    std::map<uint64_t, std::list<Order>> asks_;
    
    // Order index for O(1) lookup
    std::unordered_map<uint64_t, Order*> orders_;
    
    // Internal operations (assume lock is held)
    void add_limit_order(const Order& order);
    void match_market_order(Order order);
    void execute_trade(Order& incoming, Order& resting, uint64_t exec_qty);
};

