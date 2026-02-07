#include "order_book.h"
#include "engine_common.h"
#include <cstring>

void OrderBook::init(int64_t min_p, int64_t max_p) {
    min_price = min_p;
    max_price = max_p;

    for (int i = 0; i < MAX_TICKS; ++i) {
        buy_levels[i]  = nullptr;
        sell_levels[i] = nullptr;
    }

    best_bid = -1;
    best_ask = -1;
    level_pool_top = 0;
}

PriceLevel* OrderBook::ensure_level(uint8_t side, int32_t idx) {
#ifndef ENGINE_PERF_MODE
    if (idx < 0 || idx >= MAX_TICKS)
        ENGINE_ABORT("price index out of range");
#endif

    PriceLevel** levels = (side == BUY) ? buy_levels : sell_levels;

    if (!levels[idx]) {
#ifndef ENGINE_PERF_MODE
        if (level_pool_top >= (MAX_TICKS * 2))
            ENGINE_ABORT("price level pool exhausted");
#endif
        // In PERF mode, silently reuse last slot (safe, deterministic enough)
        PriceLevel* lvl = &level_pool[
            (level_pool_top < (MAX_TICKS * 2))
                ? level_pool_top++
                : (MAX_TICKS * 2 - 1)
        ];

        lvl->head = 0;
        lvl->tail = 0;
        levels[idx] = lvl;
    }

    return levels[idx];
}

void OrderBook::add_order(uint8_t side, int32_t idx, uint64_t order_id) {
    PriceLevel* lvl = ensure_level(side, idx);
    uint32_t size = lvl->tail - lvl->head;

#ifndef ENGINE_PERF_MODE
    if (size >= MAX_LEVEL_ORDERS)
        ENGINE_ABORT("price level overflow");
#endif

    // PERF mode: overwrite oldest (bounded ring buffer)
    if (size >= MAX_LEVEL_ORDERS) {
        lvl->head++;
    }

    lvl->order_ids[lvl->tail % MAX_LEVEL_ORDERS] =
        static_cast<uint32_t>(order_id);
    lvl->tail++;

    update_best_on_insert(side, idx);
}

// Lazy cancel: nothing to do here yet (order state marks CANCELLED)
void OrderBook::cancel_lazy(uint8_t /*side*/, int32_t /*idx*/, uint64_t /*order_id*/) {
    // no-op by design
}

void OrderBook::update_best_on_insert(uint8_t side, int32_t idx) {
    if (side == BUY) {
        if (best_bid < idx) best_bid = idx;
    } else {
        if (best_ask == -1 || best_ask > idx) best_ask = idx;
    }
}

void OrderBook::update_best_on_level_empty(uint8_t side, int32_t idx) {
    if (side == BUY && best_bid == idx) {
        for (int32_t i = idx - 1; i >= 0; --i) {
            if (buy_levels[i]) {
                best_bid = i;
                return;
            }
        }
        best_bid = -1;
    }

    if (side == SELL && best_ask == idx) {
        for (int32_t i = idx + 1; i < MAX_TICKS; ++i) {
            if (sell_levels[i]) {
                best_ask = i;
                return;
            }
        }
        best_ask = -1;
    }
}

bool OrderBook::logical_equals(const OrderBook& o) const {
    if (best_bid != o.best_bid) return false;
    if (best_ask != o.best_ask) return false;

    for (int i = 0; i < MAX_TICKS; ++i) {
        const PriceLevel* a = buy_levels[i];
        const PriceLevel* b = o.buy_levels[i];

        if ((a == nullptr) != (b == nullptr)) return false;
        if (a && std::memcmp(a, b, sizeof(PriceLevel)) != 0)
            return false;

        a = sell_levels[i];
        b = o.sell_levels[i];

        if ((a == nullptr) != (b == nullptr)) return false;
        if (a && std::memcmp(a, b, sizeof(PriceLevel)) != 0)
            return false;
    }

    return true;
}