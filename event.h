#pragma once
#include <cstdint>

// =======================
// Event Types
// =======================

enum class EventType : uint8_t {
    NEW_ORDER = 1,
    CANCEL = 2,
    RISK_CONTROL = 3,
    TIME_PULSE = 4,
    MARKET_ORDER = 5
};

// =======================
// Order Events
// =======================

struct NewOrderEvent {
    uint64_t account_id;
    uint8_t  side;      // 0 = BUY, 1 = SELL
    int64_t  price;
    int64_t  quantity;
};

struct MarketOrderEvent {
    uint64_t account_id;
    uint8_t  side;      // 0 = BUY, 1 = SELL
    int64_t  quantity;
};

struct CancelEvent {
    uint64_t order_id;
};

// =======================
// Risk Events
// =======================

enum class RiskCommand : uint8_t {
    ACCOUNT_FREEZE = 1,
    PURGE_ORDERS = 2,
    LIQUIDATION_MARKET = 3
};

struct RiskControlEvent {
    uint64_t grc_sequence;
    RiskCommand command;
    uint64_t account_id;
    int64_t  quantity;
};

struct TimePulseEvent {
    uint64_t logical_time;
};

// =======================
// Engine Event
// =======================

struct EventHeader {
    uint64_t sequence;
    EventType type;
};

struct EngineEvent {
    EventHeader header;
    union {
        NewOrderEvent    new_order;
        MarketOrderEvent market;
        CancelEvent      cancel;
        RiskControlEvent risk;
        TimePulseEvent   time;
    };
};