#include "order_book.h"
#include <algorithm>
#include <stdexcept>

// === Write operations ===

template <typename LP>
std::atomic<uint64_t> OrderBook<LP>::next_trade_id_{1};

template <typename LP>
AddResult OrderBook<LP>::add_order(const Order& order) {
    typename LP::write_lock lk(mtx_);

    if (order.quantity == 0 || order.remaining == 0 || order.remaining > order.quantity)
        return {false, {}};

    if (orders_.find(order.id) != orders_.end())
        return {false, {}};

    std::vector<Trade> trades;

    if (order.type == OrderType::LIMIT) {
        if (!add_limit_order(order, trades))
            return {false, {}};
    } else {
        Order working = order;
        match_order(working, trades);
    }

    return {true, std::move(trades)};
}

template <typename LP>
bool OrderBook<LP>::cancel_order(uint64_t order_id) {
    typename LP::write_lock lk(mtx_);

    auto it = orders_.find(order_id);
    if (it == orders_.end())
        return false;

    auto order_iter = it->second;
    Order* order_ptr = *order_iter;
    uint64_t price = order_ptr->price;
    auto& levels = (order_ptr->side == Side::BUY) ? bids_ : asks_;
    auto& level_orders = levels[price];

    level_orders.erase(order_iter);

    if (level_orders.empty())
        levels.erase(price);

    orders_.erase(it);
    pool_.deallocate(order_ptr);
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
uint64_t OrderBook<LP>::executable_quantity(const Order& order) const {
    uint64_t available = 0;

    auto add_level = [&](const std::list<Order*>& level_orders) {
        for (const Order* resting : level_orders) {
            // Saturate at the incoming quantity so the sum cannot overflow.
            if (resting->remaining >= order.remaining - available) {
                available = order.remaining;
                return true;
            }
            available += resting->remaining;
        }
        return false;
    };

    if (order.side == Side::BUY) {
        for (const auto& [price, level_orders] : asks_) {
            if (price > order.price) break;
            if (add_level(level_orders)) break;
        }
    } else {
        for (auto it = bids_.rbegin(); it != bids_.rend(); ++it) {
            if (it->first < order.price) break;
            if (add_level(it->second)) break;
        }
    }

    return available;
}

template <typename LP>
bool OrderBook<LP>::add_limit_order(const Order& order, std::vector<Trade>& trades) {
    const uint64_t available = executable_quantity(order);

    // FOK is accepted by the engine but produces no trades when the full quantity
    // is not immediately executable at prices within the order's limit.
    if (order.tif == TimeInForce::FOK && available < order.remaining)
        return true;

    // If no match can free capacity, reserve storage before doing any work. This
    // makes pool exhaustion an atomic rejection with no book side effects.
    Order* reserved_slot = nullptr;
    const bool needs_resting_slot =
        order.tif == TimeInForce::GTC && available < order.remaining;
    if (needs_resting_slot && available == 0) {
        reserved_slot = pool_.allocate(order);
        if (reserved_slot == nullptr)
            return false;
    }

    Order working = order;
    if (available > 0)
        match_order(working, trades);

    // IOC and FOK never rest an unfilled remainder.
    if (order.tif != TimeInForce::GTC)
        return true;

    if (working.remaining > 0) {
        // If the order matched anything but still has a remainder, every
        // executable resting order was consumed and at least one pool slot was
        // freed. Otherwise the slot was reserved before matching.
        if (reserved_slot == nullptr)
            reserved_slot = pool_.allocate(working);
        if (reserved_slot == nullptr)
            throw std::logic_error(
                "matching consumed liquidity but did not free a pool slot");
        *reserved_slot = working;
        auto& level_orders = (order.side == Side::BUY ? bids_ : asks_)[order.price];
        orders_[order.id] = level_orders.insert(level_orders.end(), reserved_slot);
    }

    return true;
}

template <typename LP>
void OrderBook<LP>::match_order(Order& order, std::vector<Trade>& trades) {
    // BUY matches against asks (cheapest first = begin)
    // SELL matches against bids (most expensive first = rbegin)
    bool is_buy = (order.side == Side::BUY);
    auto& levels = is_buy ? asks_ : bids_;

    while (order.remaining > 0 && !levels.empty()) {
        auto it = is_buy ? levels.begin() : std::prev(levels.end());

        if (order.type == OrderType::LIMIT) {
            if (is_buy && it->first > order.price) break;
            if (!is_buy && it->first < order.price) break;
        }

        auto& level_orders = it->second;

        for (Order* resting : level_orders) {
            if (order.remaining == 0) break;
            if (resting->remaining == 0) continue;

            uint64_t exec_qty = std::min(order.remaining, resting->remaining);
            execute_trade(order, *resting, exec_qty, trades);
        }

        // Collect filled pointers before removing, then deallocate
        std::vector<Order*> filled;
        level_orders.remove_if([&filled](Order* o) {
            if (o->is_filled()) { filled.push_back(o); return true; }
            return false;
        });
        for (Order* o : filled) pool_.deallocate(o);

        if (level_orders.empty())
            levels.erase(it);
    }
}

template <typename LP>
void OrderBook<LP>::execute_trade(Order& incoming, Order& resting, uint64_t qty,
                                   std::vector<Trade>& trades) {
    incoming.remaining -= qty;
    resting.remaining -= qty;

    uint64_t tid = next_trade_id_.fetch_add(1, std::memory_order_relaxed);
    Trade t;
    t.trade_id     = tid;
    t.symbol_id    = resting.symbol_id;
    t.price        = resting.price;
    t.quantity     = qty;
    t.buy_order_id  = (incoming.side == Side::BUY) ? incoming.id : resting.id;
    t.sell_order_id = (incoming.side == Side::SELL) ? incoming.id : resting.id;
    trades.push_back(t);

    if (resting.is_filled())
        orders_.erase(resting.id);
}

// === Explicit Instantiation ===
template class OrderBook<MutexPolicy>;
template class OrderBook<SharedMutexPolicy>;
template class OrderBook<ProcessWideMutexPolicy>;
