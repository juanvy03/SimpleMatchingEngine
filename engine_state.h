#pragma once
#include <cstdint>
#include "orders.h"
#include "order_book.h"



constexpr uint32_t MAX_ACCOUNTS = 1'000'000;

// Dust account
constexpr uint64_t DUST_ACCOUNT_ID = 0;

// Fees
constexpr int64_t FEE_NUMERATOR   = 5;
constexpr int64_t FEE_DENOMINATOR = 10'000;

enum class AccountState : uint8_t {
    ACTIVE,
    FROZEN
};

struct Balance {
    __int128 available = 0;
    __int128 locked = 0;
};

struct Account {
    AccountState state = AccountState::ACTIVE;
    Balance base;
    Balance quote;
};

struct EngineState {
    uint64_t last_sequence = 0;
    uint64_t last_grc_sequence = 0;

    Account   accounts[MAX_ACCOUNTS];
    Orders    orders;
    OrderBook book;
};

inline void zero_state(EngineState& s) {
    std::memset(&s, 0, sizeof(EngineState));
}