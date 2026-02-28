#include "engine.h"
#include "perf.h"
#include "engine_common.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

// =======================
// Helpers
// =======================

static inline __int128 fee_ceiling(__int128 value) {
    return (value * FEE_NUMERATOR + FEE_DENOMINATOR - 1) / FEE_DENOMINATOR;
}

static inline void fatal(const char*) {
    ENGINE_ABORT("reason");
}

// Forward decl
static void cancel_one(EngineState& st, uint64_t oid);

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

static inline uint64_t rdtsc() {
#if defined(__x86_64__) || defined(_M_X64)
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#else
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
// BUY fee charging (incremental, rounding-safe)
// =======================

static inline void charge_buy_fee_incremental(EngineState& st,
                                             uint64_t buy_oid,
                                             __int128 trade_value) {
    Orders& orders = st.orders;
    const uint64_t buyer_acct_id = orders.account_id[buy_oid];
    Account& buyer = st.accounts[buyer_acct_id];

    // accumulate executed notional
    const int64_t prev_exec = orders.exec_notional[buy_oid];
    const int64_t add_exec  = static_cast<int64_t>(trade_value);
    const int64_t new_exec  = prev_exec + add_exec;
    orders.exec_notional[buy_oid] = new_exec;

    // fee due on total executed so far
    const __int128 fee_due_128 = fee_ceiling(static_cast<__int128>(new_exec));
    const int64_t fee_due = static_cast<int64_t>(fee_due_128);

    const int64_t prev_fee = orders.fee_charged[buy_oid];
    const int64_t delta_fee = fee_due - prev_fee;

    if (delta_fee > 0) {
        buyer.quote.locked -= static_cast<__int128>(delta_fee);
        st.accounts[DUST_ACCOUNT_ID].quote.available += static_cast<__int128>(delta_fee);
        orders.fee_charged[buy_oid] = fee_due;
    }
}

// =======================
// Finalize BUY order (refund leftover reserved funds)
// =======================

static inline void finalize_buy_order(EngineState& st, uint64_t buy_oid) {
    Orders& orders = st.orders;
    const uint64_t acct_id = orders.account_id[buy_oid];
    Account& acct = st.accounts[acct_id];

    const int64_t reserved_notional = orders.notional_reserved[buy_oid];
    const int64_t exec_notional     = orders.exec_notional[buy_oid];
    const int64_t fee_reserved      = orders.fee_reserved[buy_oid];
    const int64_t fee_charged       = orders.fee_charged[buy_oid];

    const int64_t refund_notional = reserved_notional - exec_notional;
    const int64_t refund_fee      = fee_reserved - fee_charged;

    const __int128 refund =
        static_cast<__int128>(refund_notional) + static_cast<__int128>(refund_fee);

    if (refund > 0) {
        acct.quote.locked    -= refund;
        acct.quote.available += refund;
    }

    // Clear bookkeeping (optional but helps debugging)
    orders.notional_reserved[buy_oid] = 0;
    orders.fee_reserved[buy_oid]      = 0;
    orders.exec_notional[buy_oid]     = 0;
    orders.fee_charged[buy_oid]       = 0;
}

// =======================
// Cancel helper (safe)
// =======================

static void cancel_one(EngineState& st, uint64_t oid) {
    Orders& orders = st.orders;

    if (oid == 0 || oid >= orders.next_order_id)
        return;

    if (orders.state[oid] != OrderState::LIVE)
        return;

    const uint64_t acct_id = orders.account_id[oid];
    Account& acct = st.accounts[acct_id];

    const int64_t rem = orders.qty_remaining[oid];
    if (rem <= 0) {
        orders.state[oid] = OrderState::CANCELLED;
        return;
    }

    if (orders.side[oid] == OrderSide::BUY) {
        // refund leftover reserved funds for BUY
        finalize_buy_order(st, oid);
    } else {
        // refund remaining base for SELL
        acct.base.locked    -= static_cast<__int128>(rem);
        acct.base.available += static_cast<__int128>(rem);
    }

    orders.state[oid] = OrderState::CANCELLED;
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
                cancel_one(state_, oid);
            }
            break;
        }

        case RiskCommand::LIQUIDATION_MARKET: {
            MarketOrderEvent m{};
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

    if (ev.quantity <= 0)
        return;

    // ----------------------------
    // CREATE ORDER FIRST (we need oid for bookkeeping)
    // ----------------------------
    const uint64_t taker_oid = orders.create(
        ev.account_id,
        (ev.side == BUY) ? OrderSide::BUY : OrderSide::SELL,
        ev.price,
        ev.quantity
    );

    // ----------------------------
    // LOCK FUNDS
    // ----------------------------
    if (ev.side == BUY) {
        const __int128 notional = static_cast<__int128>(ev.price) * static_cast<__int128>(ev.quantity);
        const __int128 fee_res  = fee_ceiling(notional);
        const __int128 lock_amt = notional + fee_res;

        if (acct.quote.available < lock_amt) {
            orders.state[taker_oid] = OrderState::CANCELLED;
            return;
        }

        acct.quote.available -= lock_amt;
        acct.quote.locked    += lock_amt;

        // bookkeeping for BUY order
        orders.notional_reserved[taker_oid] = static_cast<int64_t>(notional);
        orders.fee_reserved[taker_oid]      = static_cast<int64_t>(fee_res);
        orders.exec_notional[taker_oid]     = 0;
        orders.fee_charged[taker_oid]       = 0;

    } else {
        const __int128 q = static_cast<__int128>(ev.quantity);
        if (acct.base.available < q) {
            orders.state[taker_oid] = OrderState::CANCELLED;
            return;
        }

        acct.base.available -= q;
        acct.base.locked    += q;
    }

    // ----------------------------
    // MATCHING
    // ----------------------------
    int64_t remaining = ev.quantity;

    auto match = [&](uint8_t contra_side) {
        int32_t& best =
            (contra_side == BUY) ? book.best_bid : book.best_ask;

        PriceLevel** levels =
            (contra_side == BUY) ? book.buy_levels : book.sell_levels;

        while (remaining > 0 && best != -1) {
            const int32_t idx = best;
            const int64_t px = book.min_price + static_cast<int64_t>(idx) * TICK_SIZE;

            if ((ev.side == BUY  && px > ev.price) ||
                (ev.side == SELL && px < ev.price))
                break;

            PriceLevel* lvl = levels[idx];
            if (!lvl) break;

            while (remaining > 0 && lvl->head < lvl->tail) {
                const uint32_t maker_oid =
                    lvl->order_ids[lvl->head % MAX_LEVEL_ORDERS];

                if (orders.state[maker_oid] != OrderState::LIVE) {
                    lvl->head++;
                    continue;
                }

                const int64_t maker_rem = orders.qty_remaining[maker_oid];
                if (maker_rem <= 0) {
                    orders.state[maker_oid] = OrderState::FILLED;
                    lvl->head++;
                    continue;
                }

                const int64_t traded = std::min<int64_t>(remaining, maker_rem);

                // decrement quantities
                remaining -= traded;
                orders.qty_remaining[maker_oid] -= traded;
                orders.qty_remaining[taker_oid] -= traded;

                // Determine BUY order id and SELL order id
                const uint64_t buy_oid  = (ev.side == BUY) ? taker_oid : maker_oid;
                const uint64_t sell_oid = (ev.side == BUY) ? maker_oid : taker_oid;

                const uint64_t buyer_id  = orders.account_id[buy_oid];
                const uint64_t seller_id = orders.account_id[sell_oid];

                Account& buyer  = state_.accounts[buyer_id];
                Account& seller = state_.accounts[seller_id];

                const __int128 trade_value =
                    static_cast<__int128>(px) * static_cast<__int128>(traded);

                // ----------------------------
                // SETTLEMENT (principal)
                // ----------------------------
                buyer.base.available += static_cast<__int128>(traded);
                seller.quote.available += trade_value;

                // release principal locks
                buyer.quote.locked -= trade_value;
                seller.base.locked -= static_cast<__int128>(traded);

                // ----------------------------
                // FEES
                // ----------------------------
                // BUY side fee: rounding-safe incremental
                charge_buy_fee_incremental(state_, buy_oid, trade_value);

                // SELL side fee: charge per trade from received quote
                const __int128 sell_fee = fee_ceiling(trade_value);
                seller.quote.available -= sell_fee;
                state_.accounts[DUST_ACCOUNT_ID].quote.available += sell_fee;

                emit_trade({taker_oid, maker_oid, px, traded});

                // finalize maker if filled
                if (orders.qty_remaining[maker_oid] == 0) {
                    orders.state[maker_oid] = OrderState::FILLED;

                    // If maker was BUY, refund leftover reserved funds
                    if (orders.side[maker_oid] == OrderSide::BUY) {
                        finalize_buy_order(state_, maker_oid);
                    }

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
    // POST-MATCH: book or finalize taker
    // ----------------------------
    if (orders.qty_remaining[taker_oid] > 0) {
        const int32_t idx = book.price_to_index(ev.price);
        if (idx >= 0 && idx < MAX_TICKS) {
            book.add_order(ev.side, idx, taker_oid);
        } else {
            // outside book window -> cancel safely (refund locks)
            cancel_one(state_, taker_oid);
        }
    } else {
        orders.state[taker_oid] = OrderState::FILLED;

        // If taker was BUY, refund leftover reserved funds
        if (orders.side[taker_oid] == OrderSide::BUY) {
            finalize_buy_order(state_, taker_oid);
        }
    }
}

// =======================
// MARKET Orders (safe-ish)
// =======================

void MatchingEngine::on_market(const MarketOrderEvent& ev) {
    Account& acct = state_.accounts[ev.account_id];
    if (acct.state == AccountState::FROZEN)
        return;

    if (ev.quantity <= 0)
        return;

    // Use synthetic LIMIT with extreme price
    NewOrderEvent synthetic{};
    synthetic.account_id = ev.account_id;
    synthetic.side       = ev.side;
    synthetic.quantity   = ev.quantity;
    synthetic.price      = (ev.side == BUY) ? INT64_MAX : 0;

    on_new_order(synthetic);
}

// =======================
// Cancel / Time / Trade
// =======================

void MatchingEngine::on_cancel(const CancelEvent& ev) {
    cancel_one(state_, ev.order_id);
}

void MatchingEngine::on_time(const TimePulseEvent&) {}

void MatchingEngine::emit_trade(const Trade&) {
    // append to trade ring buffer (async)
}