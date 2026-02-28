#include "engine.h"
#include "engine_common.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

// ------------------------------------------------------------
// Perf harness notes
// - Each scenario runs in a fresh EngineState so we never hit:
//   - MAX_ORDERS
//   - OrderBook level pool exhaustion
//   - MAX_LEVEL_ORDERS overflow on a single price level
// - Prices are spread across ticks to avoid per-level overflow.
// ------------------------------------------------------------

constexpr uint64_t ACCOUNTS = 1000;        // includes dust(0); we fund 1..999
constexpr __int128 INITIAL_BALANCE = 1'000'000'000;

static EngineState* make_fresh_state() {
    auto* st = static_cast<EngineState*>(std::malloc(sizeof(EngineState)));
    if (!st) std::abort();
    std::memset(st, 0, sizeof(EngineState));

    // Fund accounts (leave dust=0 empty)
    for (uint64_t i = 1; i < ACCOUNTS; ++i) {
        st->accounts[i].base.available  = INITIAL_BALANCE;
        st->accounts[i].quote.available = INITIAL_BALANCE;
    }
    return st;
}

static inline double seconds_since(const std::chrono::high_resolution_clock::time_point& t0,
                                   const std::chrono::high_resolution_clock::time_point& t1) {
    return std::chrono::duration<double>(t1 - t0).count();
}

static void run_add_only(const char* title, uint8_t side, uint64_t ops) {
    EngineState* st = make_fresh_state();
    MatchingEngine engine(*st);

    // Price spread: walk the tick grid (bounded by MAX_TICKS)
    const int64_t MIN_P = st->book.min_price;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 1; i <= ops; ++i) {
        EngineEvent ev{};
        ev.header.sequence = i;
        ev.header.type = EventType::NEW_ORDER;

        ev.new_order.account_id = 1 + (i % (ACCOUNTS - 1));
        ev.new_order.side       = side;
        ev.new_order.price      = MIN_P + static_cast<int64_t>(i % MAX_TICKS);
        ev.new_order.quantity   = 1;

        engine.apply(ev);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    const double sec = seconds_since(t0, t1);
    std::printf("\n=== %s ===\n", title);
    std::printf("Ops:   %llu\n", (unsigned long long)ops);
    std::printf("Time:  %.3f sec\n", sec);
    std::printf("Thpt:  %.0f ops/sec\n", (double)ops / sec);

    std::free(st);
}

static void run_crossing(const char* title, uint64_t per_side_ops) {
    EngineState* st = make_fresh_state();
    MatchingEngine engine(*st);

    const int64_t MIN_P = st->book.min_price;
    uint64_t seq = 1;

    // 1) Seed maker side (SELLs across the book)
    for (uint64_t i = 0; i < per_side_ops; ++i, ++seq) {
        EngineEvent ev{};
        ev.header.sequence = seq;
        ev.header.type = EventType::NEW_ORDER;

        ev.new_order.account_id = 1 + (i % (ACCOUNTS - 1));
        ev.new_order.side       = SELL;
        ev.new_order.price      = MIN_P + static_cast<int64_t>(i % MAX_TICKS);
        ev.new_order.quantity   = 1;

        engine.apply(ev);
    }

    // 2) Cross with taker BUYs that sweep (use a very high limit price)
    auto t0 = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < per_side_ops; ++i, ++seq) {
        EngineEvent ev{};
        ev.header.sequence = seq;
        ev.header.type = EventType::NEW_ORDER;

        ev.new_order.account_id = 1 + ((i + 17) % (ACCOUNTS - 1));
        ev.new_order.side       = BUY;
        ev.new_order.price      = MIN_P + (MAX_TICKS - 1); // crosses all seeded sells
        ev.new_order.quantity   = 1;

        engine.apply(ev);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    const double sec = seconds_since(t0, t1);
    std::printf("\n=== %s ===\n", title);
    std::printf("Ops:   %llu (total)\n", (unsigned long long)(per_side_ops * 2));
    std::printf("Time:  %.3f sec (measured on crossing half)\n", sec);
    std::printf("Thpt:  %.0f ops/sec\n", (double)per_side_ops / sec);

    std::free(st);
}

static void run_add_cancel_mix(const char* title,
                               uint64_t add_ops,
                               uint64_t cancel_ops,
                               uint64_t seed) {
    EngineState* st = make_fresh_state();
    MatchingEngine engine(*st);

    const int64_t MIN_P = st->book.min_price;
    uint64_t seq = 1;

    // Add phase: deterministic ids 1..add_ops assuming all orders accepted
    for (uint64_t i = 0; i < add_ops; ++i, ++seq) {
        EngineEvent ev{};
        ev.header.sequence = seq;
        ev.header.type = EventType::NEW_ORDER;

        ev.new_order.account_id = 1 + (i % (ACCOUNTS - 1));
        ev.new_order.side       = (i & 1) ? BUY : SELL;
        ev.new_order.price      = MIN_P + static_cast<int64_t>(i % MAX_TICKS);
        ev.new_order.quantity   = 1;

        engine.apply(ev);
    }

    // Cancel phase: cancel random ids in [1, add_ops]
    std::mt19937_64 rng(seed);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < cancel_ops; ++i, ++seq) {
        const uint64_t oid = 1 + (rng() % add_ops);

        EngineEvent ev{};
        ev.header.sequence = seq;
        ev.header.type = EventType::CANCEL;
        ev.cancel.order_id = oid;

        engine.apply(ev);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    const double sec = seconds_since(t0, t1);
    std::printf("\n=== %s ===\n", title);
    std::printf("Ops:   %llu (adds + cancels)\n", (unsigned long long)(add_ops + cancel_ops));
    std::printf("Time:  %.3f sec (measured on cancels)\n", sec);
    std::printf("Thpt:  %.0f ops/sec\n", (double)cancel_ops / sec);

    std::free(st);
}

int main() {
    constexpr uint64_t OPS_1M = 1'000'000;

    run_add_only("ADD-ONLY (BUY spread)",  BUY,  OPS_1M);
    run_add_only("ADD-ONLY (SELL spread)", SELL, OPS_1M);

    // Crossing is heavier; keep it smaller.
    run_crossing("CROSSING (SELL then BUY)", 400'000);

    run_add_cancel_mix("ADD + CANCEL mix", 450'000, 450'000, 12345);

    return 0;
}