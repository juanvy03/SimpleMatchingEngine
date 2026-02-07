#include "orders.h"
#include <cassert>

void Orders::init() {
    next_order_id = 1;
}

uint64_t Orders::create(uint64_t acct,
                        OrderSide s,
                        int64_t p,
                        int64_t q) {
    uint64_t oid = next_order_id++;

    assert(oid < MAX_ORDERS);

    id[oid] = oid;
    account_id[oid] = acct;
    side[oid] = s;
    price[oid] = p;
    qty_remaining[oid] = q;
    state[oid] = OrderState::LIVE;

    return oid;
}

void Orders::cancel(uint64_t oid) {
    assert(oid < next_order_id);

    if (state[oid] == OrderState::LIVE) {
        state[oid] = OrderState::CANCELLED;
    }
}

bool Orders::logical_equals(const Orders& o) const {
    if (next_order_id != o.next_order_id)
        return false;

    for (uint64_t i = 1; i < next_order_id; ++i) {
        if (account_id[i]    != o.account_id[i])    return false;
        if (price[i]         != o.price[i])         return false;
        if (qty_remaining[i] != o.qty_remaining[i]) return false;
        if (side[i]          != o.side[i])          return false;
        if (state[i]         != o.state[i])         return false;
    }
    return true;
}