#include "snapshot_delta.h"

#include <cstdint>   // uint8_t
#include <cstddef>   // size_t
#include <cstdio>    // snprintf, rename
#include <cstdlib>   // abort, malloc, free
#include <cstring>   // memset
#include <fcntl.h>   // open
#include <unistd.h>  // write, fsync, close
#include "engine_common.h"

static void die(const char*) {
    ENGINE_ABORT("reason");
}

void write_delta_snapshot(const char* path,
                          const EngineState& base,
                          const EngineState& current) {
    char tmp[256];
    std::snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    // -----------------------
    // Allocate delta on heap
    // -----------------------
    EngineState* delta =
        static_cast<EngineState*>(std::malloc(sizeof(EngineState)));
    if (!delta)
        die("malloc");

    std::memset(delta, 0, sizeof(EngineState));

    const uint8_t* b = reinterpret_cast<const uint8_t*>(&base);
    const uint8_t* c = reinterpret_cast<const uint8_t*>(&current);
    uint8_t*       d = reinterpret_cast<uint8_t*>(delta);

    for (size_t i = 0; i < sizeof(EngineState); ++i)
        d[i] = static_cast<uint8_t>(b[i] ^ c[i]);

    // -----------------------
    // Write snapshot
    // -----------------------
    int fd = ::open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0)
        die("open");

    DeltaSnapshotHeader hdr{};
    hdr.base_sequence  = base.last_sequence;
    hdr.delta_sequence = current.last_sequence;
    hdr.size           = sizeof(EngineState);

    if (::write(fd, &hdr, sizeof(hdr))
        != static_cast<ssize_t>(sizeof(hdr)))
        die("write header");

    if (::write(fd, delta, sizeof(EngineState))
        != static_cast<ssize_t>(sizeof(EngineState)))
        die("write delta");

    if (::fsync(fd) != 0)
        die("fsync");

    ::close(fd);
    std::free(delta);

    if (::rename(tmp, path) != 0)
        die("rename");
}