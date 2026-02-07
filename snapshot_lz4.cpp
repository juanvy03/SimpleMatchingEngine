#include "snapshot_lz4.h"

#include <lz4.h>

#include <cstdio>    // snprintf, rename
#include <cstdlib>   // abort, malloc, free
#include <cstring>   // memcpy
#include <fcntl.h>   // open
#include <unistd.h>  // write, fsync, close
#include "engine_common.h"

constexpr uint32_t SNAPSHOT_MAGIC_LZ4 = 0x53504C34; // "SPL4"
constexpr uint32_t SNAPSHOT_VERSION_LZ4 = 1;

static void die(const char*) {
    ENGINE_ABORT("reason");
}

void write_snapshot_lz4(const char* path, const EngineState& state) {
    char tmp[256];
    std::snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    const size_t src_size = sizeof(EngineState);

    // LZ4 API limitation: sizes must fit in int
    if (src_size > static_cast<size_t>(INT32_MAX))
        die("EngineState too large for LZ4");

    const char* src = reinterpret_cast<const char*>(&state);
    const int src_size_i = static_cast<int>(src_size);

    const int max_dst = LZ4_compressBound(src_size_i);
    if (max_dst <= 0)
        die("LZ4_compressBound");

    char* dst = static_cast<char*>(std::malloc(static_cast<size_t>(max_dst)));
    if (!dst)
        die("malloc");

    const int compressed =
        LZ4_compress_default(src, dst, src_size_i, max_dst);

    if (compressed <= 0)
        die("LZ4_compress_default");

    int fd = ::open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0)
        die("open");

    CompressedSnapshotHeader hdr{};
    hdr.magic = SNAPSHOT_MAGIC_LZ4;
    hdr.version = SNAPSHOT_VERSION_LZ4;
    hdr.last_sequence = state.last_sequence;
    hdr.last_grc_sequence = state.last_grc_sequence;
    hdr.uncompressed_size = static_cast<uint64_t>(src_size);
    hdr.compressed_size   = static_cast<uint64_t>(compressed);

    if (::write(fd, &hdr, sizeof(hdr)) != static_cast<ssize_t>(sizeof(hdr)))
        die("write header");

    if (::write(fd, dst, static_cast<size_t>(compressed))
        != static_cast<ssize_t>(compressed))
        die("write data");

    if (::fsync(fd) != 0)
        die("fsync");

    ::close(fd);
    std::free(dst);

    if (::rename(tmp, path) != 0)
        die("rename");
}

void read_snapshot_lz4(const char* path, EngineState& state) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0)
        die("open");

    CompressedSnapshotHeader hdr{};
    if (::read(fd, &hdr, sizeof(hdr)) != static_cast<ssize_t>(sizeof(hdr)))
        die("read header");

    if (hdr.magic != SNAPSHOT_MAGIC_LZ4)
        die("bad magic");

    if (hdr.version != SNAPSHOT_VERSION_LZ4)
        die("bad version");

    if (hdr.uncompressed_size != sizeof(EngineState))
        die("state size mismatch");

    if (hdr.compressed_size > static_cast<uint64_t>(INT32_MAX))
        die("compressed snapshot too large");

    char* buf =
        static_cast<char*>(std::malloc(static_cast<size_t>(hdr.compressed_size)));
    if (!buf)
        die("malloc");

    if (::read(fd, buf, static_cast<size_t>(hdr.compressed_size))
        != static_cast<ssize_t>(hdr.compressed_size))
        die("read data");

    const int decompressed =
        LZ4_decompress_safe(
            buf,
            reinterpret_cast<char*>(&state),
            static_cast<int>(hdr.compressed_size),
            static_cast<int>(hdr.uncompressed_size));

    if (decompressed < 0)
        die("LZ4_decompress_safe");

    ::close(fd);
    std::free(buf);
}