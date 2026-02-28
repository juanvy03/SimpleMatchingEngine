#pragma once
#include "engine_state.h"
#include "engine_common.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

// Simple abort helper (keeps your ENGINE_ABORT behavior)
static inline void INVARIANT_FAIL(const char* /*msg*/) {
    ENGINE_ABORT("reason");
}

struct InvariantChecker {
    // ------------------------------------------------------------
    // 1) Supply conservation + non-negative balances
    //    accounts_to_check lets fuzz only sum the accounts it uses
    // ------------------------------------------------------------
    static void check_balances(const EngineState& state,
                               __int128 base_supply,
                               __int128 quote_supply,
                               uint64_t accounts_to_check = MAX_ACCOUNTS) {
#ifndef ENGINE_PERF_MODE
        __int128 base_total  = 0;
        __int128 quote_total = 0;

        for (uint64_t i = 0; i < accounts_to_check; ++i) {
            const auto& a = state.accounts[i];

            // No negatives anywhere
            if (a.base.available < 0)  INVARIANT_FAIL("neg base avail");
            if (a.base.locked < 0)     INVARIANT_FAIL("neg base locked");
            if (a.quote.available < 0) INVARIANT_FAIL("neg quote avail");
            if (a.quote.locked < 0)    INVARIANT_FAIL("neg quote locked");

            base_total  += a.base.available;
            base_total  += a.base.locked;

            quote_total += a.quote.available;
            quote_total += a.quote.locked;
        }

        if (base_total != base_supply)
            INVARIANT_FAIL("base invariant");

        if (quote_total != quote_supply)
            INVARIANT_FAIL("quote invariant");
#else
        (void)state; (void)base_supply; (void)quote_supply; (void)accounts_to_check;
#endif
    }

    // ------------------------------------------------------------
    // 2) Orders sanity (no LIVE with qty_remaining==0, bounds)
    // ------------------------------------------------------------
    static void check_orders_sanity(const EngineState& state) {
#ifndef ENGINE_PERF_MODE
        const uint64_t n = state.orders.next_order_id;

        // next_order_id must be inside table
        if (n >= MAX_ORDERS)
            INVARIANT_FAIL("next_order_id out of bounds");

        for (uint64_t oid = 1; oid < n; ++oid) {
            // Order IDs should match slot (your Orders::create does this)
            if (state.orders.id[oid] != oid)
                INVARIANT_FAIL("order id mismatch");

            // Account ids must be in range
            if (state.orders.account_id[oid] >= MAX_ACCOUNTS)
                INVARIANT_FAIL("order account_id out of bounds");

            // LIVE orders must have remaining > 0
            if (state.orders.state[oid] == OrderState::LIVE &&
                state.orders.qty_remaining[oid] <= 0) {
                INVARIANT_FAIL("LIVE order has no remaining qty");
            }
        }
#endif
    }

    // ------------------------------------------------------------
    // 3) Book consistency:
    //    - every oid referenced in book is valid and matches side/price-level
    //    - every LIVE order with qty_remaining>0 must appear in book
    //
    // NOTE: your book is "lazy cancel", so CANCELLED/FILLED may still appear
    //       inside FIFO arrays — that's fine. We only *require* LIVE orders
    //       to appear at least once.
    // ------------------------------------------------------------
    static void check_book_consistency(const EngineState& state) {
#ifndef ENGINE_PERF_MODE
        const uint64_t n = state.orders.next_order_id;
        if (n == 0 || n >= MAX_ORDERS)
            INVARIANT_FAIL("bad next_order_id");

        // Track which LIVE orders we saw in the book
        std::vector<uint8_t> seen(static_cast<size_t>(n), 0);

        auto scan_side = [&](uint8_t side,
                             PriceLevel* const* levels) {
            for (int32_t idx = 0; idx < MAX_TICKS; ++idx) {
                const PriceLevel* lvl = levels[idx];
                if (!lvl) continue;

                // size should never exceed MAX_LEVEL_ORDERS in your implementation
                const uint32_t size = lvl->tail - lvl->head;
                if (size > MAX_LEVEL_ORDERS)
                    INVARIANT_FAIL("level overflow size");

                for (uint32_t k = 0; k < size; ++k) {
                    const uint32_t pos = lvl->head + k;
                    const uint32_t oid = lvl->order_ids[pos % MAX_LEVEL_ORDERS];

                    if (oid == 0 || oid >= n)
                        INVARIANT_FAIL("book references invalid oid");

                    // Side must match where it is stored
                    if ((side == BUY && state.orders.side[oid] != OrderSide::BUY) ||
                        (side == SELL && state.orders.side[oid] != OrderSide::SELL)) {
                        INVARIANT_FAIL("book side mismatch");
                    }

                    // Price must map to this idx
                    const int64_t price = state.orders.price[oid];
                    const int32_t expect_idx =
                        static_cast<int32_t>((price - state.book.min_price) / TICK_SIZE);

                    if (expect_idx != idx)
                        INVARIANT_FAIL("book price-level mismatch");

                    // If the order is LIVE and has remaining qty, it must be present
                    if (state.orders.state[oid] == OrderState::LIVE &&
                        state.orders.qty_remaining[oid] > 0) {
                        seen[oid] = 1;
                    }
                }
            }
        };

        scan_side(BUY,  state.book.buy_levels);
        scan_side(SELL, state.book.sell_levels);

        // Every LIVE order with qty_remaining>0 must appear in book
        for (uint64_t oid = 1; oid < n; ++oid) {
            if (state.orders.state[oid] == OrderState::LIVE &&
                state.orders.qty_remaining[oid] > 0) {
                if (!seen[oid])
                    INVARIANT_FAIL("LIVE order missing from book");
            }
        }
#endif
    }

    // ------------------------------------------------------------
    // 4) One call to run "everything"
    // ------------------------------------------------------------
    static void check_all(const EngineState& state,
                          __int128 base_supply,
                          __int128 quote_supply,
                          uint64_t accounts_to_check = MAX_ACCOUNTS) {
#ifndef ENGINE_PERF_MODE
        check_balances(state, base_supply, quote_supply, accounts_to_check);
        check_orders_sanity(state);
        check_book_consistency(state);
#else
        (void)state; (void)base_supply; (void)quote_supply; (void)accounts_to_check;
#endif
    }
};