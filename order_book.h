#pragma once
#include <cstdint>
#include <cstring>
#include "orders.h"

// ---- Config ----
constexpr int32_t TICK_SIZE = 1;
constexpr int32_t MAX_TICKS = 100'000;
constexpr uint32_t MAX_LEVEL_ORDERS = 10'000;

constexpr uint8_t BUY  = 0;
constexpr uint8_t SELL = 1;

struct PriceLevel {
    uint32_t order_ids[MAX_LEVEL_ORDERS];
    uint32_t head;
    uint32_t tail;
};

struct OrderBook {
    PriceLevel* buy_levels[MAX_TICKS];
    PriceLevel* sell_levels[MAX_TICKS];

    int32_t best_bid;
    int32_t best_ask;

    int64_t min_price;
    int64_t max_price;

    PriceLevel level_pool[MAX_TICKS * 2];
    uint32_t   level_pool_top;

    void init(int64_t min_p, int64_t max_p);

    inline int32_t price_to_index(int64_t price) const {
        return static_cast<int32_t>((price - min_price) / TICK_SIZE);
    }

    PriceLevel* ensure_level(uint8_t side, int32_t idx);
    void add_order(uint8_t side, int32_t idx, uint64_t order_id);
    void cancel_lazy(uint8_t side, int32_t idx, uint64_t order_id);

    void update_best_on_insert(uint8_t side, int32_t idx);
    void update_best_on_level_empty(uint8_t side, int32_t idx);

    // ✅ MUST be here
    bool logical_equals(const OrderBook& other) const;
};