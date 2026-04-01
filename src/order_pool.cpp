#include "order_pool.h"
#include <cassert>
#include <new>

OrderPool::OrderPool(std::size_t capacity)
    : capacity_(capacity), size_(0)
{
    slots_     = new Slot[capacity];
    free_list_ = new std::size_t[capacity];
    // Initialise free list as a stack: index capacity-1 at top
    for (std::size_t i = 0; i < capacity; ++i) {
        slots_[i].in_use = false;
        free_list_[i]    = i;
    }
    free_head_ = capacity;  // stack grows downward on allocate
}

OrderPool::~OrderPool() {
    delete[] free_list_;
    delete[] slots_;
}

Order* OrderPool::allocate(const Order& order) {
    if (free_head_ == 0) return nullptr;  // pool exhausted

    std::size_t idx = free_list_[--free_head_];
    slots_[idx].in_use = true;
    Order* ptr = reinterpret_cast<Order*>(slots_[idx].storage);
    new (ptr) Order(order);  // placement-new: copy into pre-allocated slot
    ++size_;
    return ptr;
}

void OrderPool::deallocate(Order* order) {
    // storage is the first (alignas) member of Slot, so Order* == Slot*
    Slot*       slot = reinterpret_cast<Slot*>(order);
    std::size_t idx  = static_cast<std::size_t>(slot - slots_);
    assert(idx < capacity_ && slots_[idx].in_use);

    order->~Order();
    slots_[idx].in_use   = false;
    free_list_[free_head_++] = idx;
    --size_;
}
