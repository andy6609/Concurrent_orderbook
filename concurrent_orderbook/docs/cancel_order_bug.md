# cancel_order: dangling pointer after remove_if

## What happened

`test_cancel_updates_best_price` failed. The test cancels the best-priced order and
checks that `best_bid_price()` falls back to the next level. It didn't — the wrong
price level was being erased from the book.

## Root cause

`orders_` maps order ID to a raw pointer into a `std::list` node:

```cpp
orders_[order.id] = &level_orders.back();
```

In `cancel_order`, the code retrieved `order_ptr->price` to identify the price level,
then called `remove_if` to remove the order from the list:

```cpp
Order* order_ptr = it->second;
auto& levels = (order_ptr->side == Side::BUY) ? bids_ : asks_;
auto& level_orders = levels[order_ptr->price];

level_orders.remove_if([order_id](const Order& o) {
    return o.id == order_id;
});

if (level_orders.empty())
    levels.erase(order_ptr->price);  // order_ptr is dangling here
```

`std::list::remove_if` destroys the removed element and frees its node.
`order_ptr` is dangling by the time `levels.erase(order_ptr->price)` runs.
Reading `order_ptr->price` is undefined behavior. In practice it read garbage,
erasing the wrong price level (100 instead of 105), leaving the book in an
inconsistent state.

The bug was silent in the four original tests because none of them cancelled an
order and then queried the remaining best price on a multi-level book.

## Fix

Save `price` to a local variable before `remove_if`:

```cpp
Order* order_ptr = it->second;
uint64_t price = order_ptr->price;
auto& levels = (order_ptr->side == Side::BUY) ? bids_ : asks_;
auto& level_orders = levels[price];

level_orders.remove_if([order_id](const Order& o) {
    return o.id == order_id;
});

if (level_orders.empty())
    levels.erase(price);
```

`price` is copied before `remove_if` touches the list, so the erase always
targets the correct level.

## Why the original tests didn't catch it

All four original tests cancelled an order and checked `total_orders()`, which
reads from `orders_` (unaffected by the wrong erase). None of them called
`best_bid_price()` or `best_ask_price()` after a cancel on a book with multiple
price levels. The UB was reachable but the observable effect wasn't tested.
