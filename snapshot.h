#pragma once
#include "engine_state.h"
#include <cstdint>

constexpr uint32_t SNAPSHOT_MAGIC = 0x53504150; // "SPAP"
constexpr uint32_t SNAPSHOT_VERSION = 1;

struct SnapshotHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t last_sequence;
    uint64_t last_grc_sequence;
    uint64_t state_size;
};