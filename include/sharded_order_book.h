#pragma once
#include "order_book.h"
#include <unordered_map>
#include <optional>
#include <shared_mutex>

// ShardedOrderBook routes orders to per-symbol OrderBook instances.
// Each symbol has its own independent lock, so threads working on different
// symbols never contend — horizontal scaling with zero global locking.
template <typename LockPolicy = MutexPolicy>
class ShardedOrderBook {
public:
    explicit ShardedOrderBook(std::size_t pool_capacity_per_symbol = DEFAULT_POOL_CAPACITY);

    // Returns {accepted, trades}. Creates a new book for symbol on first use.
    AddResult add_order(const Order& order);

    // Returns false if symbol or order_id not found.
    bool cancel_order(uint32_t symbol_id, uint64_t order_id);

    std::optional<uint64_t> best_bid_price(uint32_t symbol_id) const;
    std::optional<uint64_t> best_ask_price(uint32_t symbol_id) const;

    std::size_t symbol_count() const;

private:
    // Lazily created per-symbol books.
    // Outer map protected by a shared_mutex; each inner book has its own lock.
    mutable std::shared_mutex registry_mtx_;
    std::unordered_map<uint32_t, OrderBook<LockPolicy>> books_;
    std::size_t pool_capacity_per_symbol_;

    // Get-or-create book for a symbol (acquires write lock on registry).
    OrderBook<LockPolicy>& get_or_create(uint32_t symbol_id);

    // Const lookup — returns nullptr if symbol doesn't exist.
    const OrderBook<LockPolicy>* find(uint32_t symbol_id) const;
};
