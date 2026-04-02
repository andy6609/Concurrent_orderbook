#include "order_book.h"
#include <algorithm>

// === Write operations ===

template <typename LP>
std::atomic<uint64_t> OrderBook<LP>::next_trade_id_{1};

template <typename LP>
AddResult OrderBook<LP>::add_order(const Order& order) {
    typename LP::write_lock lk(mtx_);

    if (orders_.find(order.id) != orders_.end())
        return {false, {}};

    std::vector<Trade> trades;

    if (order.type == OrderType::LIMIT) {
        add_limit_order(order, trades);
    } else {
        Order working = order;
        match_market_order(working, trades);
    }

    return {true, std::move(trades)};
}

template <typename LP>
bool OrderBook<LP>::cancel_order(uint64_t order_id) {
    typename LP::write_lock lk(mtx_);

    auto it = orders_.find(order_id);
    if (it == orders_.end())
        return false;

    Order* order_ptr = it->second;
    uint64_t price = order_ptr->price;
    auto& levels = (order_ptr->side == Side::BUY) ? bids_ : asks_;
    auto& level_orders = levels[price];

    level_orders.remove_if([order_id](Order* o) { return o->id == order_id; });

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
void OrderBook<LP>::add_limit_order(const Order& order, std::vector<Trade>& trades) {
    // Check if this order crosses the book (aggressive limit order)
    bool crosses = false;
    if (order.side == Side::BUY && !asks_.empty())
        crosses = (order.price >= asks_.begin()->first);
    else if (order.side == Side::SELL && !bids_.empty())
        crosses = (order.price <= bids_.rbegin()->first);

    if (!crosses) {
        // IOC/FOK with no crossing — immediately cancel (nothing to fill)
        if (order.tif == TimeInForce::IOC || order.tif == TimeInForce::FOK)
            return;
        // GTC: allocate from pool and rest in the book
        Order* slot = pool_.allocate(order);
        auto& level_orders = (order.side == Side::BUY ? bids_ : asks_)[order.price];
        level_orders.push_back(slot);
        orders_[order.id] = slot;
        return;
    }

    // FOK: pre-check that enough quantity is available across all price levels
    if (order.tif == TimeInForce::FOK) {
        auto& opp_levels = (order.side == Side::BUY) ? asks_ : bids_;
        uint64_t available = 0;
        for (auto& [lvl_price, lvl_orders] : opp_levels) {
            if (order.side == Side::BUY  && lvl_price > order.price) break;
            if (order.side == Side::SELL && lvl_price < order.price) break;
            for (Order* o : lvl_orders)
                available += o->remaining;
            if (available >= order.remaining) break;
        }
        if (available < order.remaining)
            return;  // Kill — not enough to fill entirely
    }

    // Aggressive — match first, then handle remainder based on TIF
    Order working = order;
    match_market_order(working, trades);

    // IOC: cancel any unfilled remainder (never rests)
    if (order.tif == TimeInForce::IOC)
        return;

    // GTC: rest whatever didn't fill
    if (working.remaining > 0) {
        Order* slot = pool_.allocate(working);
        auto& level_orders = (order.side == Side::BUY ? bids_ : asks_)[order.price];
        level_orders.push_back(slot);
        orders_[order.id] = slot;
    }
}

template <typename LP>
void OrderBook<LP>::match_market_order(Order& order, std::vector<Trade>& trades) {
    // BUY matches against asks (cheapest first = begin)
    // SELL matches against bids (most expensive first = rbegin)
    bool is_buy = (order.side == Side::BUY);
    auto& levels = is_buy ? asks_ : bids_;

    while (order.remaining > 0 && !levels.empty()) {
        auto it = is_buy ? levels.begin() : std::prev(levels.end());
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
