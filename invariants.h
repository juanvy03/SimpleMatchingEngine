#pragma once
#include "engine_state.h"
#include "engine_common.h"

struct InvariantChecker {
    static void check_balances(const EngineState& state,
                               __int128 base_supply,
                               __int128 quote_supply) {
#ifndef ENGINE_PERF_MODE
        __int128 base_total = 0;
        __int128 quote_total = 0;

        for (uint64_t i = 0; i < MAX_ACCOUNTS; ++i) {
            base_total  += state.accounts[i].base.available;
            base_total  += state.accounts[i].base.locked;
            quote_total += state.accounts[i].quote.available;
            quote_total += state.accounts[i].quote.locked;
        }

        if (base_total != base_supply)
            ENGINE_ABORT("base invariant");

        if (quote_total != quote_supply)
            ENGINE_ABORT("quote invariant");
#endif
    }
};