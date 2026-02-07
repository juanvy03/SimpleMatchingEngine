#pragma once
#include "engine_state.h"
#include <cstdint>

struct DeltaSnapshotHeader {
    uint64_t base_sequence;
    uint64_t delta_sequence;
    uint64_t size;
};