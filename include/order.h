#pragma once
#include <cstdint>
#include <iostream>
#include <vector>

enum class Side {
    BUY,
    SELL
};

enum class OrderType {
    LIMIT,
    MARKET
};

struct Order {
    uint64_t id;
    uint32_t symbol_id;
    
    OrderType type;
    Side side;
    
    uint64_t price;
    uint64_t quantity;
    uint64_t remaining;
    
    static Order Limit(uint64_t id, uint32_t symbol, Side side, 
                       uint64_t price, uint64_t qty) {
        return Order{id, symbol, OrderType::LIMIT, side, price, qty, qty};
    }
    
    static Order Market(uint64_t id, uint32_t symbol, Side side, uint64_t qty) {
        return Order{id, symbol, OrderType::MARKET, side, 0, qty, qty};
    }
    
    bool is_filled() const { return remaining == 0; }
};

struct Trade {
    uint64_t trade_id;
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    uint32_t symbol_id;
    uint64_t price;
    uint64_t quantity;
};

struct AddResult {
    bool accepted;
    std::vector<Trade> trades;
};

inline std::ostream& operator<<(std::ostream& os, const Order& o) {
    os << "Order{id=" << o.id 
       << " " << (o.side == Side::BUY ? "BUY" : "SELL")
       << " " << (o.type == OrderType::LIMIT ? "LIMIT" : "MARKET")
       << " price=" << o.price << " qty=" << o.remaining << "}";
    return os;
}

