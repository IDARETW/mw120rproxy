// zlib_inflate.h — thin wrapper over the vendored zlib (src/common/zlib).
// inflate_all: decompress a complete zlib stream of UNKNOWN output size (grows the output buffer).
// deflate_all: compress with the given level (default Z_DEFAULT_COMPRESSION via level=-1).
// Both operate on whole in-memory buffers (no streaming-file API). Used by the IW3 .ff codec.
#pragma once
#include <cstdint>
#include <vector>

namespace zt {

// Inflate a zlib stream (zlib header, e.g. 0x78 0x01). Returns true on Z_STREAM_END; out holds the
// decompressed bytes. On failure returns false and out is whatever was produced so far (cleared).
bool inflate_all(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
bool inflate_all(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out);

// Deflate (zlib-wrapped) the input at `level` (0..9, or -1 = Z_DEFAULT_COMPRESSION). Returns true on
// success; out holds the compressed stream.
bool deflate_all(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, int level = -1);
bool deflate_all(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out, int level = -1);

} // namespace zt
