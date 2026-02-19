#include "order_book.h"
#include <algorithm>

// === Write operations ===

template <typename LP>
bool OrderBook<LP>::add_order(const Order& order) {
    typename LP::write_lock lk(mtx_);

    if (orders_.find(order.id) != orders_.end())
        return false;

    if (order.type == OrderType::LIMIT)
        add_limit_order(order);
    else
        match_market_order(order);

    return true;
}

template <typename LP>
bool OrderBook<LP>::cancel_order(uint64_t order_id) {
    typename LP::write_lock lk(mtx_);

    auto it = orders_.find(order_id);
    if (it == orders_.end())
        return false;

    Order* order_ptr = it->second;
    auto& levels = (order_ptr->side == Side::BUY) ? bids_ : asks_;
    auto& level_orders = levels[order_ptr->price];

    level_orders.remove_if([order_id](const Order& o) {
        return o.id == order_id;
    });

    if (level_orders.empty())
        levels.erase(order_ptr->price);

    orders_.erase(it);
    return true;
}

// === Read operations ===

template <typename LP>
std::optional<uint64_t> OrderBook<LP>::best_bid_price() const {
    typename LP::read_lock lk(mtx_);
    if (bids_.empty()) return std::nullopt;
    return bids_.rbegin()->first;
}

template <typename LP>
std::optional<uint64_t> OrderBook<LP>::best_ask_price() const {
    typename LP::read_lock lk(mtx_);
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

template <typename LP>
size_t OrderBook<LP>::total_orders() const {
    typename LP::read_lock lk(mtx_);
    return orders_.size();
}

template <typename LP>
size_t OrderBook<LP>::total_bid_levels() const {
    typename LP::read_lock lk(mtx_);
    return bids_.size();
}

template <typename LP>
size_t OrderBook<LP>::total_ask_levels() const {
    typename LP::read_lock lk(mtx_);
    return asks_.size();
}

// === Internal (lock already held) ===

template <typename LP>
void OrderBook<LP>::add_limit_order(const Order& order) {
    auto& levels = (order.side == Side::BUY) ? bids_ : asks_;
    auto& level_orders = levels[order.price];
    level_orders.push_back(order);
    orders_[order.id] = &level_orders.back();
}

template <typename LP>
void OrderBook<LP>::match_market_order(Order order) {
    auto& levels = (order.side == Side::BUY) ? asks_ : bids_;

    while (order.remaining > 0 && !levels.empty()) {
        auto it = levels.begin();
        auto& level_orders = it->second;

        for (auto& resting : level_orders) {
            if (order.remaining == 0) break;
            if (resting.remaining == 0) continue;

            uint64_t exec_qty = std::min(order.remaining, resting.remaining);
            execute_trade(order, resting, exec_qty);
        }

        level_orders.remove_if([](const Order& o) { return o.is_filled(); });

        if (level_orders.empty())
            levels.erase(it);
    }
}

template <typename LP>
void OrderBook<LP>::execute_trade(Order& incoming, Order& resting, uint64_t qty) {
    incoming.remaining -= qty;
    resting.remaining -= qty;

    if (resting.is_filled())
        orders_.erase(resting.id);
}

// === Explicit Instantiation ===
template class OrderBook<MutexPolicy>;
template class OrderBook<SharedMutexPolicy>;
