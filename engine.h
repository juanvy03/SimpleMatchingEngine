#pragma once
#include "event.h"
#include "engine_state.h"

struct Trade {
    uint64_t taker_order_id;
    uint64_t maker_order_id;
    int64_t  price;
    int64_t  quantity;
};

class MatchingEngine {
public:
    explicit MatchingEngine(EngineState& state);

    // THE ONLY ENTRY POINT
    void apply(const EngineEvent& event);

private:
    EngineState& state_;

    void on_new_order(const NewOrderEvent&);
    void on_cancel(const CancelEvent&);
    void on_risk(const RiskControlEvent&);
    void on_time(const TimePulseEvent&);

    void emit_trade(const Trade& t);
    void on_market(const MarketOrderEvent&);
};