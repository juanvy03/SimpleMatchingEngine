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
    // Core order fields
    uint64_t   id[MAX_ORDERS];
    uint64_t   account_id[MAX_ORDERS];
    int64_t    price[MAX_ORDERS];
    int64_t    qty_remaining[MAX_ORDERS];
    OrderSide  side[MAX_ORDERS];
    OrderState state[MAX_ORDERS];

    // ---- Lock/Fee accounting (only meaningful for BUY orders) ----
    // qty_initial: original order quantity at creation
    int64_t qty_initial[MAX_ORDERS];

    // notional_reserved = limit_price * qty_initial (BUY only, else 0)
    int64_t notional_reserved[MAX_ORDERS];

    // fee_reserved = fee_ceiling(notional_reserved) (BUY only, else 0)
    int64_t fee_reserved[MAX_ORDERS];

    // exec_notional: sum of actual traded notional so far (BUY only)
    int64_t exec_notional[MAX_ORDERS];

    // fee_charged: fee already taken so far (BUY only)
    int64_t fee_charged[MAX_ORDERS];

    uint64_t next_order_id;

    void init();

    uint64_t create(uint64_t account_id,
                    OrderSide side,
                    int64_t price,
                    int64_t quantity);

    void cancel(uint64_t order_id);

    bool logical_equals(const Orders& o) const;
};