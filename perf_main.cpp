#include "engine.h"
#include "engine_common.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr uint64_t ORDERS = 1'000'000;
constexpr uint64_t ACCOUNTS = 1000;

int main() {
    auto* state =
        static_cast<EngineState*>(std::malloc(sizeof(EngineState)));
    std::memset(state, 0, sizeof(EngineState));

    for (uint64_t i = 0; i < ACCOUNTS; ++i) {
        state->accounts[i].base.available  = 1'000'000'000;
        state->accounts[i].quote.available = 1'000'000'000;
    }

    MatchingEngine engine(*state);

    auto start = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 1; i <= ORDERS; ++i) {
        EngineEvent ev{};
        ev.header.sequence = i;
        ev.header.type = EventType::NEW_ORDER;
        ev.new_order.account_id = i % ACCOUNTS;
        ev.new_order.side = BUY;
        ev.new_order.price = 1'000'000;
        ev.new_order.quantity = 1;

        engine.apply(ev);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double seconds =
        std::chrono::duration<double>(end - start).count();

    std::printf("Orders: %llu\n",
            static_cast<unsigned long long>(ORDERS));
    std::printf("Time: %.3f sec\n", seconds);
    std::printf("Throughput: %.0f ops/sec\n", ORDERS / seconds);
}