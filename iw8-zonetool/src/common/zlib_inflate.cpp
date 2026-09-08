// zlib_inflate.cpp — implementation over vendored zlib. Streaming inflate to grow unknown output.
#include "zlib_inflate.h"
#include "log.h"
#include "zlib.h" // src/common/zlib (on include path via build.bat)
#include <cstring>

namespace zt {

bool inflate_all(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    out.clear();
    if (!in || inLen == 0)
        return false;

    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) {
        zt::err("inflateInit failed");
        return false;
    }

    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in));
    zs.avail_in = static_cast<uInt>(inLen);

    // Grow the output in chunks; start at ~4x input (zone blobs are highly compressible).
    size_t cap = inLen * 4 + 0x10000;
    out.resize(cap);

    int ret = Z_OK;
    for (;;) {
        if (zs.total_out >= out.size())
            out.resize(out.size() * 2);
        zs.next_out = reinterpret_cast<Bytef*>(out.data() + zs.total_out);
        zs.avail_out = static_cast<uInt>(out.size() - zs.total_out);

        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_END)
            break;
        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            zt::err("inflate failed: %d (%s)", ret, zs.msg ? zs.msg : "?");
            inflateEnd(&zs);
            out.clear();
            return false;
        }
        if (ret == Z_BUF_ERROR && zs.avail_in == 0)
            break; // truncated but no more input
    }

    out.resize(zs.total_out);
    inflateEnd(&zs);
    return ret == Z_STREAM_END;
}

bool inflate_all(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    return inflate_all(in.data(), in.size(), out);
}

bool deflate_all(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out, int level) {
    out.clear();
    if (level < -1 || level > 9)
        level = Z_DEFAULT_COMPRESSION;

    uLong bound = compressBound(static_cast<uLong>(inLen));
    out.resize(bound);
    uLongf destLen = bound;
    int ret = compress2(out.data(), &destLen, reinterpret_cast<const Bytef*>(in),
                        static_cast<uLong>(inLen), level);
    if (ret != Z_OK) {
        zt::err("compress2 failed: %d", ret);
        out.clear();
        return false;
    }
    out.resize(destLen);
    return true;
}

bool deflate_all(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, int level) {
    return deflate_all(in.data(), in.size(), out, level);
}

} // namespace zt
