#pragma once
#include "iw8_structs.h"
#include "iw8_zonebuffer.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace iw8
{

class ZoneWriter
{
  public:
    using AssetWriteFn = std::function<void(ZoneWriter &)>;

    // ---- per-asset registration
    // -------------------------------------------------------------------
    struct Entry
    {
        IW8_XAssetType type;
        std::string name;
        AssetWriteFn body; // writes the asset struct + trailing data; framing is automatic
    };
    void add(IW8_XAssetType type, const std::string &name, AssetWriteFn body)
    {
        entries_.push_back({type, name, std::move(body)});
    }
    size_t assetCount() const
    {
        return entries_.size();
    }

    // ---- low-level stream API forwarded to the underlying ZoneBuffer
    // ------------------------------
    void write(const void *d, size_t n)
    {
        zb_.write(d, n);
    }
    template <typename T> void writeT(const T &v)
    {
        zb_.writeT(v);
    }
    void writeStr(const char *s)
    {
        zb_.writeStr(s);
    }
    void writeStr(const std::string &s)
    {
        zb_.writeStr(s.c_str());
    }
    void align(uint64_t mask)
    {
        zb_.align(mask);
    }
    void pushStream(XFileBlock s)
    {
        zb_.pushStream(s);
    }
    void popStream()
    {
        zb_.popStream();
    }

    // tagged pointer helpers (raw IW8 sentinels — no mask)
    void writeFollows()
    {
        uint64_t v = PTR_FOLLOWS;
        zb_.write(&v, 8);
    } // -2
    void writeInsert()
    {
        uint64_t v = PTR_INSERT;
        zb_.write(&v, 8);
    } // -3
    void writeNull()
    {
        uint64_t v = PTR_NULL;
        zb_.write(&v, 8);
    } //  0
    void writeShared()
    {
        uint64_t v = PTR_SHARED;
        zb_.write(&v, 8);
    } // -1
    // packed cross-reference offset (positive). value+stream packing matches the loader's
    // DB_ResolvePackedOffsetAddress; not used by the stub writers but exposed for cross-refs.
    void writePacked(uint32_t streamIdx, uint32_t value)
    {
        uint64_t v = (static_cast<uint64_t>(streamIdx & 0xF) << 32) | (value + 1);
        zb_.write(&v, 8);
    }
    // writeXString — emit an inline XString to the CURRENT stream: the 8-byte PTR_FOLLOWS(-2) tag
    // word THEN the NUL-terminated chars. REQUIRED for every const char* loaded via Load_XString
    // (asset name/baseName): Load_XString RE-READS its own 8-byte tag from the current stream (not
    // the struct body) and switches on it (0/-1/-2/packed). Writing only the raw chars (writeStr)
    // makes the loader read the first 8 chars (e.g. "maps/mp/") AS the tag -> packed-resolve ->
    // garbage ptr / field aliases to chars+8 ("mp_test.") -> AV in DB_MarkAsset. The struct-body -2
    // stamp is dead (overwritten by the re-read). NON-XString pointer fields (nodes/cells/etc.) do
    // NOT use this — they read the struct value
    // + inline data.
    void writeXString(const char *s)
    {
        writeFollows();
        zb_.writeStr(s);
    }
    void writeXString(const std::string &s)
    {
        writeFollows();
        zb_.writeStr(s.c_str());
    }

    // ---- emit
    // ------------------------------------------------------------------------------------- Frame
    // the registered assets in load-read order and invoke each body. After build(), call
    // body()/blockSize()/totalDecompressed() to hand off to ff_io.
    void build();

    const std::vector<uint8_t> &body() const
    {
        return zb_.data();
    }
    uint64_t blockSize(int i) const
    {
        return zb_.streamSize(i);
    }
    uint64_t totalDecompressed() const
    {
        uint64_t t = 0;
        for (int i = 0; i < IW8_MAX_XFILE_COUNT; ++i)
            t += zb_.streamSize(i);
        return t;
    }
    // Reserve calc/zero-fill (stream-4) region bytes WITHOUT emitting body bytes (see
    // ZoneBuffer::reserveCalc).
    void reserveCalc(size_t n)
    {
        zb_.reserveCalc(n);
    }
    uint64_t calcSize() const
    {
        return zb_.calcSize();
    } // == ΣblockSize - body().size()
    ZoneBuffer &buffer()
    {
        return zb_;
    }

  private:
    ZoneBuffer zb_;
    std::vector<Entry> entries_;
};

// Convenience: build a valid-EMPTY zone (XAssetList assetCount=0). Used for eng/ww companions.
void buildEmptyZone(ZoneBuffer &zb);

} // namespace iw8
