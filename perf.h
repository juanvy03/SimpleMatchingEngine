#pragma once
#include <cstdint>

struct PerfSample {
    uint64_t seq;
    uint64_t start_tsc;
    uint64_t end_tsc;
};

constexpr uint32_t PERF_BUFFER_SIZE = 1'000'000;

struct PerfRing {
    PerfSample samples[PERF_BUFFER_SIZE];
    uint32_t   head = 0;

    inline void record(uint64_t seq,
                       uint64_t start,
                       uint64_t end) {
        samples[head++ % PERF_BUFFER_SIZE] = {seq, start, end};
    }
};