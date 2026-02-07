#pragma once
#include <cstdint>

constexpr uint32_t MAX_ORDERS = 2'000'000;

enum class OrderSide : uint8_t {
    BUY  = 0,
    SELL = 1
};

enum class OrderState : uint8_t {
    LIVE,
    CANCELLED,
    FILLED
};

struct Orders {
    uint64_t  id[MAX_ORDERS];
    uint64_t  account_id[MAX_ORDERS];
    int64_t   price[MAX_ORDERS];
    int64_t   qty_remaining[MAX_ORDERS];
    OrderSide side[MAX_ORDERS];
    OrderState state[MAX_ORDERS];

    uint64_t next_order_id;

    void init();
    uint64_t create(uint64_t account_id,
                    OrderSide side,
                    int64_t price,
                    int64_t quantity);

    void cancel(uint64_t order_id);
    bool logical_equals(const Orders& o) const;
};

