#include "snapshot.h"
#include "engine_common.h"

#include <cstdio>      // snprintf, rename
#include <fcntl.h>     // open
#include <unistd.h>    // write, fsync, close
#include <cstdlib>     // abort
#include <cstring>    // memset

static void die(const char*) {
    ENGINE_ABORT("reason");
}

void write_snapshot(const char* path, const EngineState& state) {
    char tmp_path[256];
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    int fd = ::open(tmp_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) die("open snapshot");

    SnapshotHeader hdr{};
    hdr.magic = SNAPSHOT_MAGIC;
    hdr.version = SNAPSHOT_VERSION;
    hdr.last_sequence = state.last_sequence;
    hdr.last_grc_sequence = state.last_grc_sequence;
    hdr.state_size = sizeof(EngineState);

    if (::write(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr))
        die("write header");

    if (::write(fd, &state, sizeof(state)) != (ssize_t)sizeof(state))
        die("write state");

    if (::fsync(fd) != 0)
        die("fsync");

    ::close(fd);

    if (::rename(tmp_path, path) != 0)
        die("rename snapshot");
}