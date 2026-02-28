#include "engine.h"
#include "invariants.h"
#include "engine_state.h"

#include <random>
#include <vector>
#include <cstdlib>
#include <cstdio>

constexpr uint64_t TEST_ACCOUNTS    = 1000;
constexpr __int128 INITIAL_BALANCE  = 1'000'000'000;
constexpr uint64_t EVENTS           = 500'000;

// Avoid slow full scan in invariants if you added accounts_to_check support.
// If your invariants.h doesn’t have accounts_to_check, remove that argument.
static void assert_deterministic_equal(const EngineState& a,
                                       const EngineState& b) {
    if (a.last_sequence != b.last_sequence) std::abort();
    if (a.last_grc_sequence != b.last_grc_sequence) std::abort();

    if (a.orders.next_order_id != b.orders.next_order_id) std::abort();
    if (!a.orders.logical_equals(b.orders)) std::abort();
    if (!a.book.logical_equals(b.book)) std::abort();

    for (uint64_t i = 0; i < TEST_ACCOUNTS; ++i) {
        const Account& x = a.accounts[i];
        const Account& y = b.accounts[i];
        if (x.state != y.state) std::abort();
        if (x.base.available != y.base.available) std::abort();
        if (x.base.locked != y.base.locked) std::abort();
        if (x.quote.available != y.quote.available) std::abort();
        if (x.quote.locked != y.quote.locked) std::abort();
    }
}

int main() {
    auto* state = new EngineState{};
    zero_state(*state);
    MatchingEngine engine(*state);

    // DUST is account 0 -> must start empty
    for (uint64_t i = 1; i < TEST_ACCOUNTS; ++i) {
        state->accounts[i].base.available  = INITIAL_BALANCE;
        state->accounts[i].quote.available = INITIAL_BALANCE;
    }

    std::mt19937_64 rng(12345);

    std::vector<EngineEvent> log;
    log.reserve(EVENTS);

    auto rand_acct = [&]() -> uint64_t {
        return 1 + (rng() % (TEST_ACCOUNTS - 1));
    };

    auto rand_side = [&]() -> uint8_t {
        return (uint8_t)(rng() % 2);
    };

    auto rand_price = [&]() -> int64_t {
        return 1'000'000 + (int64_t)(rng() % 1000);
    };

    auto rand_qty = [&]() -> int64_t {
        return 1 + (int64_t)(rng() % 10);
    };

    for (uint64_t seq = 1; seq <= EVENTS; ++seq) {
        EngineEvent ev{};
        ev.header.sequence = seq;

        // Weighted random mix
        // 70% NEW, 15% CANCEL, 10% MARKET, 4% RISK, 1% TIME
        uint64_t r = rng() % 100;

        if (r < 70) {
            ev.header.type = EventType::NEW_ORDER;
            ev.new_order.account_id = rand_acct();
            ev.new_order.side       = rand_side();
            ev.new_order.price      = rand_price();
            ev.new_order.quantity   = rand_qty();
        } else if (r < 85) {
            ev.header.type = EventType::CANCEL;
            // pick any oid in range; engine cancel path is safe/idempotent
            uint64_t max_oid = state->orders.next_order_id;
            ev.cancel.order_id = (max_oid > 1) ? (1 + (rng() % (max_oid - 1))) : 1;
        } else if (r < 95) {
            ev.header.type = EventType::MARKET_ORDER;
            ev.market.account_id = rand_acct();
            ev.market.side       = rand_side();
            ev.market.quantity   = rand_qty();
        } else if (r < 99) {
            ev.header.type = EventType::RISK_CONTROL;
            ev.risk.grc_sequence = seq; // monotonic in this test
            ev.risk.account_id   = rand_acct();
            ev.risk.quantity     = rand_qty();

            uint64_t cmd = rng() % 3;
            ev.risk.command = (cmd == 0)
                ? RiskCommand::ACCOUNT_FREEZE
                : (cmd == 1)
                    ? RiskCommand::PURGE_ORDERS
                    : RiskCommand::LIQUIDATION_MARKET;
        } else {
            ev.header.type = EventType::TIME_PULSE;
            ev.time.logical_time = seq;
        }

        log.push_back(ev);
        engine.apply(ev);
    }

    // Replay
    auto* replay = new EngineState{};
    zero_state(*replay);
    MatchingEngine replay_engine(*replay);

    for (uint64_t i = 1; i < TEST_ACCOUNTS; ++i) {
        replay->accounts[i].base.available  = INITIAL_BALANCE;
        replay->accounts[i].quote.available = INITIAL_BALANCE;
    }

    for (const auto& ev : log) {
        replay_engine.apply(ev);
    }

    assert_deterministic_equal(*state, *replay);

    // Invariants: total supply excludes dust initial funding
    const __int128 base_supply  = INITIAL_BALANCE * (TEST_ACCOUNTS - 1);
    const __int128 quote_supply = INITIAL_BALANCE * (TEST_ACCOUNTS - 1);

    // If your invariants.h has accounts_to_check, pass TEST_ACCOUNTS.
    // Otherwise, call the 3-arg version.
    InvariantChecker::check_balances(*state, base_supply, quote_supply);

    delete state;
    delete replay;
    return 0;
}