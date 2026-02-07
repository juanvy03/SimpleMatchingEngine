#include "engine.h"
#include "invariants.h"
#include "engine_state.h"

#include <random>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>

constexpr uint64_t TEST_ACCOUNTS = 1000;
constexpr __int128 INITIAL_BALANCE = 1'000'000'000;

// ------------------------------------------------------------
// Local fee helper (must match engine.cpp exactly)
// ------------------------------------------------------------
static inline __int128 fee_ceiling_local(__int128 value) {
    return (value * FEE_NUMERATOR + FEE_DENOMINATOR - 1) / FEE_DENOMINATOR;
}

// ------------------------------------------------------------
// Deterministic comparison (NO raw memcmp)
// ------------------------------------------------------------
static void assert_deterministic_equal(const EngineState& a,
                                       const EngineState& b) {
    if (a.last_sequence != b.last_sequence) {
        std::fprintf(stderr,
            "Mismatch: last_sequence %llu vs %llu\n",
            (unsigned long long)a.last_sequence,
            (unsigned long long)b.last_sequence);
        std::abort();
    }

    if (a.orders.next_order_id != b.orders.next_order_id) {
        std::fprintf(stderr,
            "Mismatch: next_order_id %llu vs %llu\n",
            (unsigned long long)a.orders.next_order_id,
            (unsigned long long)b.orders.next_order_id);
        std::abort();
    }

    if (!a.orders.logical_equals(b.orders)) {
        std::fprintf(stderr, "Mismatch: orders table\n");
        std::abort();
    }

    if (std::memcmp(&a.accounts, &b.accounts, sizeof(a.accounts)) != 0) {
        std::fprintf(stderr, "Mismatch: accounts\n");
        std::abort();
    }

    if (!a.book.logical_equals(b.book)) {
        std::fprintf(stderr, "Mismatch: order book\n");
        std::abort();
    }
}

int main() {
    // ----------------------------
    // PRIMARY ENGINE
    // ----------------------------
    auto* state = new EngineState{};
    zero_state(*state);
    MatchingEngine engine(*state);

    for (uint64_t i = 0; i < TEST_ACCOUNTS; ++i) {
        state->accounts[i].base.available  = INITIAL_BALANCE;
        state->accounts[i].quote.available = INITIAL_BALANCE;
    }

    std::mt19937_64 rng(12345);
    std::vector<EngineEvent> log;
    log.reserve(500'000);

    for (uint64_t i = 1; i <= 500'000; ++i) {
        EngineEvent ev{};
        ev.header.sequence = i;
        ev.header.type = EventType::NEW_ORDER;

        ev.new_order.account_id = rng() % TEST_ACCOUNTS;
        ev.new_order.side       = rng() % 2;
        ev.new_order.price      = 1'000'000 + (rng() % 1000);
        ev.new_order.quantity   = (rng() % 10) + 1;

        log.push_back(ev);
        engine.apply(ev);
    }

    // ----------------------------
    // REPLAY ENGINE
    // ----------------------------
    auto* replay = new EngineState{};
    zero_state(*replay);
    MatchingEngine replay_engine(*replay);

    for (uint64_t i = 0; i < TEST_ACCOUNTS; ++i) {
        replay->accounts[i].base.available  = INITIAL_BALANCE;
        replay->accounts[i].quote.available = INITIAL_BALANCE;
    }

    for (const auto& ev : log)
        replay_engine.apply(ev);

    // ----------------------------
    // DETERMINISM CHECK
    // ----------------------------
    assert_deterministic_equal(*state, *replay);

    // ------------------------------------------------
    // FINAL REFUND OF ALL RESTING BUY ORDERS
    // (fuzz has NO cancel events → mandatory)
    // ------------------------------------------------
    for (uint64_t oid = 1; oid < state->orders.next_order_id; ++oid) {
        if (state->orders.state[oid] != OrderState::LIVE)
            continue;

        if (state->orders.side[oid] != OrderSide::BUY)
            continue;

        int64_t rem = state->orders.qty_remaining[oid];
        if (rem <= 0)
            continue;

        uint64_t acct_id = state->orders.account_id[oid];
        Account& acct = state->accounts[acct_id];

        __int128 notional =
            (__int128)state->orders.price[oid] * rem;
        __int128 fee = fee_ceiling_local(notional);

        acct.quote.locked    -= (notional + fee);
        acct.quote.available += (notional + fee);

        state->orders.state[oid] = OrderState::CANCELLED;
    }

    // ----------------------------
    // BALANCE INVARIANTS
    // ----------------------------
    InvariantChecker::check_balances(
        *state,
        INITIAL_BALANCE * TEST_ACCOUNTS,
        INITIAL_BALANCE * TEST_ACCOUNTS
    );

    delete state;
    delete replay;
    return 0;
}