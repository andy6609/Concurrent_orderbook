#include "sharded_order_book.h"

// ── Constructor ───────────────────────────────────────────────────────────────

template <typename LP>
ShardedOrderBook<LP>::ShardedOrderBook(std::size_t pool_capacity_per_symbol)
    : pool_capacity_per_symbol_(pool_capacity_per_symbol) {}

// ── Private helpers ───────────────────────────────────────────────────────────

template <typename LP>
OrderBook<LP>& ShardedOrderBook<LP>::get_or_create(uint32_t symbol_id) {
    // Fast path: already exists (shared lock)
    {
        std::shared_lock<std::shared_mutex> rl(registry_mtx_);
        auto it = books_.find(symbol_id);
        if (it != books_.end()) return it->second;
    }
    // Slow path: create under exclusive lock
    std::unique_lock<std::shared_mutex> wl(registry_mtx_);
    // Re-check after acquiring write lock (another thread may have created it)
    auto it = books_.find(symbol_id);
    if (it != books_.end()) return it->second;

    books_.emplace(std::piecewise_construct,
                   std::forward_as_tuple(symbol_id),
                   std::forward_as_tuple(pool_capacity_per_symbol_));
    return books_.at(symbol_id);
}

template <typename LP>
const OrderBook<LP>* ShardedOrderBook<LP>::find(uint32_t symbol_id) const {
    std::shared_lock<std::shared_mutex> rl(registry_mtx_);
    auto it = books_.find(symbol_id);
    return (it != books_.end()) ? &it->second : nullptr;
}

// ── Public API ────────────────────────────────────────────────────────────────

template <typename LP>
AddResult ShardedOrderBook<LP>::add_order(const Order& order) {
    return get_or_create(order.symbol_id).add_order(order);
}

template <typename LP>
bool ShardedOrderBook<LP>::cancel_order(uint32_t symbol_id, uint64_t order_id) {
    std::shared_lock<std::shared_mutex> rl(registry_mtx_);
    auto it = books_.find(symbol_id);
    if (it == books_.end()) return false;
    return it->second.cancel_order(order_id);
}

template <typename LP>
std::optional<uint64_t> ShardedOrderBook<LP>::best_bid_price(uint32_t symbol_id) const {
    const auto* book = find(symbol_id);
    return book ? book->best_bid_price() : std::nullopt;
}

template <typename LP>
std::optional<uint64_t> ShardedOrderBook<LP>::best_ask_price(uint32_t symbol_id) const {
    const auto* book = find(symbol_id);
    return book ? book->best_ask_price() : std::nullopt;
}

template <typename LP>
std::size_t ShardedOrderBook<LP>::symbol_count() const {
    std::shared_lock<std::shared_mutex> rl(registry_mtx_);
    return books_.size();
}

// ── Explicit instantiation ────────────────────────────────────────────────────
template class ShardedOrderBook<MutexPolicy>;
template class ShardedOrderBook<SharedMutexPolicy>;
