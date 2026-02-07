#pragma once

#include "engine_state.h"
#include <cstdint>

// =======================
// LZ4 Snapshot Header
// =======================

struct CompressedSnapshotHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t last_sequence;
    uint64_t last_grc_sequence;
    uint64_t uncompressed_size;
    uint64_t compressed_size;
};

// =======================
// API
// =======================

// Write full EngineState snapshot compressed with LZ4
void write_snapshot_lz4(const char* path,
                        const EngineState& state);

// Read full EngineState snapshot compressed with LZ4
void read_snapshot_lz4(const char* path,
                       EngineState& state);