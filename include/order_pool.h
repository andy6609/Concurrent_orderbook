#pragma once
#include "order.h"
#include <cstddef>
#include <cstdint>

// Fixed-size memory pool for Order objects.
// Pre-allocates a contiguous block of slots; allocate/deallocate are O(1)
// and avoid hitting the OS allocator on every order add/cancel/fill.
class OrderPool {
public:
    explicit OrderPool(std::size_t capacity);
    ~OrderPool();

    // Returns a pointer to an initialised Order slot, or nullptr if full.
    Order* allocate(const Order& order);

    // Returns the slot back to the pool. Pointer must have come from allocate().
    void deallocate(Order* order);

    std::size_t capacity() const { return capacity_; }
    std::size_t size()     const { return size_; }

private:
    struct Slot {
        alignas(Order) unsigned char storage[sizeof(Order)];
        bool in_use;
    };

    Slot*       slots_;
    std::size_t capacity_;
    std::size_t size_;

    // Free-list: indices of available slots.
    std::size_t* free_list_;
    std::size_t  free_head_;

    // Non-copyable.
    OrderPool(const OrderPool&)            = delete;
    OrderPool& operator=(const OrderPool&) = delete;
};
