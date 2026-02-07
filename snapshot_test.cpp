#include "engine.h"
#include "snapshot_lz4.h"
#include "engine_common.h"

#include <cstring>
#include <cstdlib>

int main() {
    // -----------------------
    // Allocate on heap
    // -----------------------
    auto* state = new EngineState{};
    MatchingEngine engine(*state);

    // Seed balances
    for (uint64_t i = 0; i < 100; ++i) {
        state->accounts[i].base.available  = 1'000'000;
        state->accounts[i].quote.available = 1'000'000;
    }

    write_snapshot_lz4("test.snap", *state);

    // Restore into heap object
    auto* restored = new EngineState{};
    read_snapshot_lz4("test.snap", *restored);

    if (std::memcmp(state, restored, sizeof(EngineState)) != 0)
        ENGINE_ABORT("reason");

    delete state;
    delete restored;
    return 0;
}