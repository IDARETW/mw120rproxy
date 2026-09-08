// Sequential Replay zone data with independent memory-stream reservations.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <stack>

namespace iw8 {

enum XFileBlock : uint8_t {
    XFILE_BLOCK_TEMP = 0,
    XFILE_BLOCK_TEMP_PRELOAD = 1,
    XFILE_BLOCK_TEMP_POSTLOAD = 2,
    XFILE_BLOCK_IMAGE_STREAM = 3,
    XFILE_BLOCK_SHARED_STREAM = 4,
    XFILE_BLOCK_VIRTUAL =
        5, // BULK asset bodies/geometry -> retail stream 5 / region 0 (was dev's 8 = unallocated)
    XFILE_BLOCK_RUNTIME = 6,
    XFILE_BLOCK_UNK7 = 7,
    XFILE_BLOCK_CALLBACK = 8,
    XFILE_BLOCK_SCRIPT = 9,
    XFILE_BLOCK_UNK10 = 10,
    IW8_MAX_XFILE_COUNT = 11
};

// tagged-pointer sentinels (raw values — Load_RawFilePtr compares == -1/-2/-3 exactly)
static constexpr uint64_t PTR_NULL = 0ull;
static constexpr uint64_t PTR_FOLLOWS = (uint64_t)-2; // data follows inline (the common case)
static constexpr uint64_t PTR_INSERT =
    (uint64_t)-3; // follows + DB_InsertPointer (top-level asset ptrs)
static constexpr uint64_t PTR_SHARED = (uint64_t)-1; // shared stream

class ZoneBuffer {
  public:
    ZoneBuffer() : stream_(XFILE_BLOCK_TEMP), streamSize_(IW8_MAX_XFILE_COUNT, 0) {}

    void write(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + n);
        streamSize_[stream_] += n;
    }
    template <typename T> void writeT(const T& v) {
        write(&v, sizeof(T));
    }
    void writeStr(const char* s) {
        write(s, std::strlen(s) + 1);
    } // incl NUL

    // reserveCalc — reserve `n` bytes in the CURRENT stream WITHOUT emitting body bytes. For a CALC /
    // zero-fill stream (retail stream 4 = SHARED_STREAM, the gfxWorld dpvs region): the loader's
    // stream-load primitive sub_140001E4A610 takes the `curStream==4 -> memset(dest,0,size)` branch (NOT
    // the copy-from-body branch), so the reserved region is zero-filled at load and NOTHING is copied
    // from the Oodle body. This grows blockSize[stream_] (so DB_AllocXZoneMemory reserves a real region
    // base) but NOT buf_ — so a non-null PTR_FOLLOWS dpvs field resolves into a committed, zeroed backing
    // instead of the null/short stream-4 region that memset(NULL)'d at load (retail 0x48E3954 via
    // 0x1E4A638). Tracked in calcSize_ so the .ff writer can decouple XFile.size (= body raw len = the
    // single Oodle frame's decompressed length) from ΣblockSize (= body + calc). Do NOT align() before a
    // reserveCalc — align() materializes zero BODY bytes and would desync body.size() from XFile.size.
    // (workflow w98wrqmz9: streamSem.calc_consumes_body_bytes=false.)
    void reserveCalc(size_t n) {
        streamSize_[stream_] += n;
        calcSize_ += n;
    }

    // Replay DB_PatchMem_FixStreamAlignment (RVA 0xD8D480) changes the memory
    // cursor only. It never reads the fastfile. Charge padding to the reservation,
    // not the serialized body; calcSize tracks all bytes without disk payload.
    void align(uint64_t mask) {
        if (mask) {
            const uint64_t cur = streamSize_[stream_];
            const uint64_t aligned = (~mask & (mask + cur));
            const uint64_t pad = aligned - cur;
            streamSize_[stream_] = aligned;
            calcSize_ += pad;
        }
    }

    void pushStream(XFileBlock s) {
        stack_.push(stream_);
        stream_ = s;
    }
    void popStream() {
        stream_ = stack_.top();
        stack_.pop();
    }

    uint64_t streamSize(int i) const {
        return streamSize_[i];
    }
    uint64_t calcSize() const {
        return calcSize_;
    } // bytes reserved via reserveCalc (NOT in buf_)
    const std::vector<uint8_t>& data() const {
        return buf_;
    }
    size_t size() const {
        return buf_.size();
    }

  private:
    std::vector<uint8_t> buf_;
    std::vector<uint64_t> streamSize_;
    XFileBlock stream_;
    std::stack<XFileBlock> stack_;
    uint64_t calcSize_ = 0;
};

} // namespace iw8
