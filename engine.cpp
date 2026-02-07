#include "engine.h"
#include "perf.h"
#include <algorithm>
#include <cstdlib>
#include "engine_common.h"

// =======================
// Helpers
// =======================

static inline __int128 fee_ceiling(__int128 value) {
    return (value * FEE_NUMERATOR + FEE_DENOMINATOR - 1) / FEE_DENOMINATOR;
}

static inline void fatal(const char*) {
    ENGINE_ABORT("reason");
}

// =======================
// Constructor
// =======================

MatchingEngine::MatchingEngine(EngineState& state)
    : state_(state) {
    state_.orders.init();

    const int64_t MIN_P = 1'000'000;
    const int64_t MAX_P = MIN_P + MAX_TICKS;
    state_.book.init(MIN_P, MAX_P);
}

// =======================
// Dispatcher
// =======================
extern PerfRing g_perf;

inline uint64_t rdtsc() {
#if defined(__x86_64__) || defined(_M_X64)
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    // Fallback (non-x86)
    return 0;
#endif
}

void MatchingEngine::apply(const EngineEvent& event) {
    uint64_t start = rdtsc();
    if (event.header.sequence != state_.last_sequence + 1)
        fatal("sequence violation");

    state_.last_sequence = event.header.sequence;

    switch (event.header.type) {
        case EventType::NEW_ORDER:     on_new_order(event.new_order); break;
        case EventType::CANCEL:        on_cancel(event.cancel); break;
        case EventType::RISK_CONTROL:  on_risk(event.risk); break;
        case EventType::TIME_PULSE:    on_time(event.time); break;
        case EventType::MARKET_ORDER:  on_market(event.market); break;
    }
    uint64_t end = rdtsc();
    g_perf.record(event.header.sequence, start, end);
}

// =======================
// Risk Control
// =======================

void MatchingEngine::on_risk(const RiskControlEvent& rce) {
    if (rce.grc_sequence <= state_.last_grc_sequence)
        fatal("GRC ordering violation");

    state_.last_grc_sequence = rce.grc_sequence;

    Account& acct = state_.accounts[rce.account_id];

    switch (rce.command) {
        case RiskCommand::ACCOUNT_FREEZE:
            acct.state = AccountState::FROZEN;
            break;

        case RiskCommand::PURGE_ORDERS: {
            for (uint64_t oid = 1; oid < state_.orders.next_order_id; ++oid) {
                if (state_.orders.account_id[oid] != rce.account_id) continue;
                if (state_.orders.state[oid] != OrderState::LIVE) continue;

                int64_t rem = state_.orders.qty_remaining[oid];
                if (rem <= 0) continue;

                if (state_.orders.side[oid] == OrderSide::BUY) {
                    __int128 notional = (__int128)state_.orders.price[oid] * rem;
                    __int128 fee = fee_ceiling(notional);
                    acct.quote.locked    -= (notional + fee);
                    acct.quote.available += (notional + fee);
                } else {
                    acct.base.locked    -= rem;
                    acct.base.available += rem;
                }

                state_.orders.state[oid] = OrderState::CANCELLED;
            }
            break;
        }

        case RiskCommand::LIQUIDATION_MARKET: {
            MarketOrderEvent m;
            m.account_id = rce.account_id;
            m.quantity   = rce.quantity;

            // decide side deterministically
            if (acct.base.available + acct.base.locked > 0)
                m.side = SELL;
            else
                m.side = BUY;

            on_market(m);
            break;
        }
    }
}

// =======================
// LIMIT Orders
// =======================

void MatchingEngine::on_new_order(const NewOrderEvent& ev) {
    Account& acct = state_.accounts[ev.account_id];
    if (acct.state == AccountState::FROZEN)
        return;

    Orders& orders = state_.orders;
    OrderBook& book = state_.book;

    // ----------------------------
    // LOCK FUNDS
    // ----------------------------
    __int128 lock_amount = 0;

    if (ev.side == BUY) {
        __int128 notional = (__int128)ev.price * ev.quantity;
        lock_amount = notional + fee_ceiling(notional);

        if (acct.quote.available < lock_amount)
            return;

        acct.quote.available -= lock_amount;
        acct.quote.locked    += lock_amount;
    } else {
        if (acct.base.available < ev.quantity)
            return;

        acct.base.available -= ev.quantity;
        acct.base.locked    += ev.quantity;
    }

    uint64_t taker_oid = orders.create(
        ev.account_id,
        ev.side == BUY ? OrderSide::BUY : OrderSide::SELL,
        ev.price,
        ev.quantity
    );

    int64_t remaining = ev.quantity;
    __int128 spent_notional = 0;

    // ----------------------------
    // MATCHING
    // ----------------------------
    auto match = [&](uint8_t contra_side) {
        int32_t& best =
            (contra_side == BUY) ? book.best_bid : book.best_ask;

        PriceLevel** levels =
            (contra_side == BUY) ? book.buy_levels : book.sell_levels;

        while (remaining > 0 && best != -1) {
            int32_t idx = best;
            int64_t price = book.min_price + idx * TICK_SIZE;

            if ((ev.side == BUY  && price > ev.price) ||
                (ev.side == SELL && price < ev.price))
                break;

            PriceLevel* lvl = levels[idx];
            if (!lvl) break;

            while (remaining > 0 && lvl->head < lvl->tail) {
                uint32_t maker_oid =
                    lvl->order_ids[lvl->head % MAX_LEVEL_ORDERS];

                if (orders.state[maker_oid] != OrderState::LIVE) {
                    lvl->head++;
                    continue;
                }

                int64_t traded =
                    std::min(remaining, orders.qty_remaining[maker_oid]);

                remaining -= traded;
                orders.qty_remaining[maker_oid] -= traded;

                uint64_t maker_acct_id = orders.account_id[maker_oid];

                Account& buyer =
                    (ev.side == BUY)
                        ? acct
                        : state_.accounts[maker_acct_id];

                Account& seller =
                    (ev.side == BUY)
                        ? state_.accounts[maker_acct_id]
                        : acct;

                __int128 trade_value = (__int128)price * traded;

                // ----------------------------
                // SETTLEMENT (NO FEES HERE)
                // ----------------------------
                // SETTLEMENT
                buyer.base.available += traded;
                seller.quote.available += trade_value;

                // Release maker locks correctly
                if (orders.side[maker_oid] == OrderSide::BUY) {
                    // BUY maker locked (price * qty + fee)
                    __int128 maker_fee = fee_ceiling(trade_value);
                    buyer.quote.locked -= (trade_value + maker_fee);
                    state_.accounts[DUST_ACCOUNT_ID].quote.available += maker_fee;
                } else {
                    // SELL maker locked base only
                    seller.base.locked -= traded;
                }

                spent_notional += trade_value;

                emit_trade({taker_oid, maker_oid, price, traded});

                if (orders.qty_remaining[maker_oid] == 0) {
                    orders.state[maker_oid] = OrderState::FILLED;
                    lvl->head++;
                }
            }

            if (lvl->head == lvl->tail) {
                levels[idx] = nullptr;
                book.update_best_on_level_empty(contra_side, idx);
            }
        }
    };

    match(ev.side == BUY ? SELL : BUY);

    // ----------------------------
    // APPLY FEE ONCE (BOTH SIDES)
    // ----------------------------
    __int128 total_fee = fee_ceiling(spent_notional);

    if (spent_notional > 0) {
        if (ev.side == BUY) {
            // BUY taker pays fee from locked quote
            acct.quote.locked -= total_fee;
        } else {
            // SELL taker pays fee from received quote
            acct.quote.available -= total_fee;
        }

        state_.accounts[DUST_ACCOUNT_ID].quote.available += total_fee;
    }

    // ----------------------------
    // REFUND UNUSED LOCKS
    // ----------------------------
    if (ev.side == BUY) {
        __int128 refund =
            lock_amount - (spent_notional + total_fee);

        acct.quote.locked    -= refund;
        acct.quote.available += refund;
    } else {
        acct.base.locked -= (ev.quantity - remaining);
    }

    orders.qty_remaining[taker_oid] = remaining;

    if (remaining > 0) {
        int32_t idx = book.price_to_index(ev.price);
        if (idx >= 0 && idx < MAX_TICKS)
            book.add_order(ev.side, idx, taker_oid);
        else
            orders.state[taker_oid] = OrderState::CANCELLED;
    } else {
        orders.state[taker_oid] = OrderState::FILLED;
    }
}

// =======================
// MARKET Orders (safe)
// =======================

void MatchingEngine::on_market(const MarketOrderEvent& ev) {
    Account& acct = state_.accounts[ev.account_id];
    if (acct.state == AccountState::FROZEN) return;

    int64_t remaining = ev.quantity;

    while (remaining > 0 &&
           (ev.side == BUY ? state_.book.best_ask : state_.book.best_bid) != -1) {

        if (ev.side == BUY && acct.quote.available <= 0) break;
        if (ev.side == SELL && acct.base.available <= 0) break;

        // Reuse LIMIT matcher by synthetic limit
        NewOrderEvent synthetic{};
        synthetic.account_id = ev.account_id;
        synthetic.side = ev.side;
        synthetic.quantity = remaining;
        synthetic.price = (ev.side == BUY)
            ? INT64_MAX
            : 0;

        on_new_order(synthetic);
        break;
    }
}

// =======================
// Cancel / Time / Trade
// =======================

void MatchingEngine::on_cancel(const CancelEvent& ev) {
    state_.orders.cancel(ev.order_id);
}

void MatchingEngine::on_time(const TimePulseEvent&) {}

void MatchingEngine::emit_trade(const Trade&) {
    // append to trade ring buffer (async)
}