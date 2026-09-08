// bindump.h — offline reader for the ZoneTool (IW5) BinaryDumper stream format.
// Mirrors ZoneUtils/Utils/BinaryDumper.hpp's AssetReader EXACTLY (read order + the dedup `list`), but
// reads from an in-memory byte buffer instead of a FILE* and never touches game memory.
//
// Every binary dump file (.xme6 .xse .colmap .gfxmap .ents.triggers .ents.stages) is a stream of
// [1-byte DUMP_TYPE tag][payload] primitives (reference: reference/IW5_DUMP_FORMAT.md §1). The reader
// keeps the same `list` push order the writer used so DUMP_TYPE_OFFSET back-refs resolve.
//
// IMPORTANT (offline reality): the dumped structs are 32-bit IW5 layouts. We don't reconstruct host
// pointers — read_string/read_asset return the NUL-terminated NAME (a std::string), and read_array/
// read_raw return the raw little-endian bytes (a std::vector<uint8_t>) the caller parses on a 32-bit
// mirror. The dedup `list` records each emitted block's {kind, entryCount, entrySize, and a stable id}
// so an OFFSET ref re-yields the same logical value. Errors are reported via ok()/error() — a malformed
// stream sets the error flag and stops (callers must check ok()).
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>

namespace dumpsrc {

enum DumpType : uint8_t {
    DUMP_TYPE_INT    = 0,
    DUMP_TYPE_STRING = 1,
    DUMP_TYPE_ASSET  = 2,
    DUMP_TYPE_ARRAY  = 3,
    DUMP_TYPE_OFFSET = 4,
    DUMP_TYPE_FLOAT  = 5,
    DUMP_TYPE_RAW    = 6,
};

class BinReader {
public:
    BinReader(const uint8_t* data, size_t size) : p_(data), n_(size) {}
    explicit BinReader(const std::vector<uint8_t>& v) : p_(v.data()), n_(v.size()) {}

    bool ok() const { return ok_; }
    const std::string& error() const { return err_; }
    size_t pos() const { return cur_; }
    size_t remaining() const { return cur_ <= n_ ? n_ - cur_ : 0; }
    bool eof() const { return cur_ >= n_; }

    // ---- scalar primitives (tag-checked) ---------------------------------------------------------
    int32_t read_int() {
        if (!expectTag(DUMP_TYPE_INT, "int")) return 0;
        return readScalar<int32_t>();
    }
    uint32_t read_uint() {
        if (!expectTag(DUMP_TYPE_INT, "uint")) return 0;
        return readScalar<uint32_t>();
    }
    float read_float() {
        if (!expectTag(DUMP_TYPE_FLOAT, "float")) return 0.f;
        return readScalar<float>();
    }

    // ---- string: tag 1 -> [existing byte][C-string]; tag 4 -> OFFSET back-ref ---------------------
    // Returns the NUL-terminated string (empty for NULL). `wasNull` distinguishes a real empty string
    // from a NULL marker when the caller cares.
    std::string read_string(bool* wasNull = nullptr) {
        if (wasNull) *wasNull = false;
        uint8_t tag;
        if (!readTag(tag)) return {};
        if (tag == DUMP_TYPE_STRING) {
            uint8_t existing;
            if (!readByte(existing)) return {};
            if (existing == 0) { if (wasNull) *wasNull = true; return {}; } // DUMP_NONEXISTING
            std::string s = readCString();
            pushList(ListEntry{ENT_STRING, 1, 1, lastString(s)});
            return s;
        }
        if (tag == DUMP_TYPE_OFFSET) {
            int32_t li, ai;
            if (!readScalarCk(li) || !readScalarCk(ai)) return {};
            return offsetString(li, ai);
        }
        fail("read_string: unexpected tag %u", tag);
        return {};
    }

    // ---- asset: tag 2 -> [existing byte][C-string name]; tag 4 -> OFFSET. Body resolved by name. ---
    // Returns the asset NAME (empty + wasNull=true for a NULL asset).
    std::string read_asset(bool* wasNull = nullptr) {
        if (wasNull) *wasNull = false;
        uint8_t tag;
        if (!readTag(tag)) return {};
        if (tag == DUMP_TYPE_ASSET) {
            uint8_t existing;
            if (!readByte(existing)) return {};
            if (existing == 0) { if (wasNull) *wasNull = true; return {}; } // DUMP_NONEXISTING
            std::string name = readCString();
            pushList(ListEntry{ENT_ASSET, 1, 1, lastString(name)});
            return name;
        }
        if (tag == DUMP_TYPE_OFFSET) {
            int32_t li, ai;
            if (!readScalarCk(li) || !readScalarCk(ai)) return {};
            return offsetString(li, ai); // assets dedup back to a name too
        }
        fail("read_asset: unexpected tag %u", tag);
        return {};
    }

    // ---- array: tag 3 -> [u32 count][count*elemSize raw bytes]; tag 4 -> OFFSET. ------------------
    // elemSize = sizeof(IW5 32-bit T). Returns raw bytes (count*elemSize). `count` out = element count.
    std::vector<uint8_t> read_array(size_t elemSize, uint32_t& count) {
        count = 0;
        uint8_t tag;
        if (!readTag(tag)) return {};
        if (tag == DUMP_TYPE_ARRAY) {
            uint32_t n;
            if (!readScalarCk(n)) return {};
            count = n;
            if (n == 0) { return {}; }
            size_t bytes = (size_t)n * elemSize;
            if (cur_ + bytes > n_) { fail("read_array: %u*%zu overruns buffer", n, elemSize); return {}; }
            std::vector<uint8_t> out(p_ + cur_, p_ + cur_ + bytes);
            cur_ += bytes;
            pushList(ListEntry{ENT_ARRAY, n, (uint32_t)elemSize, lastArray(out)});
            return out;
        }
        if (tag == DUMP_TYPE_OFFSET) {
            int32_t li, ai;
            if (!readScalarCk(li) || !readScalarCk(ai)) return {};
            return offsetArray(li, ai, elemSize, count);
        }
        fail("read_array: unexpected tag %u", tag);
        return {};
    }

    // ---- raw: tag 6 -> [u32 size][size raw bytes]; tag 4 -> OFFSET. -------------------------------
    std::vector<uint8_t> read_raw() {
        uint8_t tag;
        if (!readTag(tag)) return {};
        if (tag == DUMP_TYPE_RAW) {
            uint32_t sz;
            if (!readScalarCk(sz)) return {};
            if (sz == 0) return {};
            if (cur_ + sz > n_) { fail("read_raw: %u overruns buffer", sz); return {}; }
            std::vector<uint8_t> out(p_ + cur_, p_ + cur_ + sz);
            cur_ += sz;
            pushList(ListEntry{ENT_ARRAY, 1, sz, lastArray(out)});
            return out;
        }
        if (tag == DUMP_TYPE_OFFSET) {
            int32_t li, ai; uint32_t dummy;
            if (!readScalarCk(li) || !readScalarCk(ai)) return {};
            return offsetArray(li, ai, 1, dummy);
        }
        fail("read_raw: unexpected tag %u", tag);
        return {};
    }

    // read_single<T> == read_array of 1 element.
    std::vector<uint8_t> read_single(size_t elemSize) { uint32_t c; return read_array(elemSize, c); }

    // Little-endian scalar field extractors over a raw-array element buffer (helpers for parsers).
    static int32_t  geti32(const std::vector<uint8_t>& b, size_t off) { return getT<int32_t>(b, off); }
    static uint32_t getu32(const std::vector<uint8_t>& b, size_t off) { return getT<uint32_t>(b, off); }
    static uint16_t getu16(const std::vector<uint8_t>& b, size_t off) { return getT<uint16_t>(b, off); }
    static int16_t  geti16(const std::vector<uint8_t>& b, size_t off) { return getT<int16_t>(b, off); }
    static uint8_t  getu8 (const std::vector<uint8_t>& b, size_t off) { return off < b.size() ? b[off] : 0; }
    static float    getf  (const std::vector<uint8_t>& b, size_t off) { return getT<float>(b, off); }

private:
    enum EntKind : uint8_t { ENT_STRING, ENT_ASSET, ENT_ARRAY };
    struct ListEntry {
        EntKind  kind;
        uint32_t entryCount;
        uint32_t entrySize;
        int32_t  storeId;   // index into strStore_ (string/asset) or arrStore_ (array)
    };

    template <typename T> static T getT(const std::vector<uint8_t>& b, size_t off) {
        T v{}; if (off + sizeof(T) <= b.size()) std::memcpy(&v, b.data() + off, sizeof(T)); return v;
    }

    bool readByte(uint8_t& out) {
        if (cur_ >= n_) { fail("eof reading byte @%zu", cur_); return false; }
        out = p_[cur_++]; return true;
    }
    bool readTag(uint8_t& out) { return readByte(out); }
    bool expectTag(DumpType want, const char* what) {
        uint8_t t;
        if (!readByte(t)) return false;
        if (t != want) { fail("read_%s: tag %u != %u", what, t, (unsigned)want); return false; }
        return true;
    }
    template <typename T> T readScalar() {
        T v{}; if (cur_ + sizeof(T) <= n_) { std::memcpy(&v, p_ + cur_, sizeof(T)); cur_ += sizeof(T); }
        else fail("eof reading scalar @%zu", cur_);
        return v;
    }
    template <typename T> bool readScalarCk(T& out) {
        if (cur_ + sizeof(T) > n_) { fail("eof reading scalar @%zu", cur_); return false; }
        std::memcpy(&out, p_ + cur_, sizeof(T)); cur_ += sizeof(T); return true;
    }
    std::string readCString() {
        std::string s;
        while (cur_ < n_) {
            char c = (char)p_[cur_++];
            if (c == '\0') return s;
            s.push_back(c);
        }
        fail("unterminated C-string @%zu", cur_);
        return s;
    }

    // dedup list stores (parallel to BinaryDumper's `list`, in identical push order).
    int32_t lastString(const std::string& s) { strStore_.push_back(s); return (int32_t)strStore_.size() - 1; }
    int32_t lastArray(const std::vector<uint8_t>& a) { arrStore_.push_back(a); return (int32_t)arrStore_.size() - 1; }
    void pushList(const ListEntry& e) { list_.push_back(e); }

    std::string offsetString(int32_t li, int32_t /*ai*/) {
        if (li < 0 || (size_t)li >= list_.size()) { fail("offset string li=%d oob", li); return {}; }
        const ListEntry& e = list_[li];
        if (e.kind == ENT_STRING || e.kind == ENT_ASSET) return strStore_[e.storeId];
        fail("offset string li=%d not a string entry", li);
        return {};
    }
    std::vector<uint8_t> offsetArray(int32_t li, int32_t ai, size_t elemSize, uint32_t& count) {
        count = 0;
        if (li < 0 || (size_t)li >= list_.size()) { fail("offset array li=%d oob", li); return {}; }
        const ListEntry& e = list_[li];
        if (e.kind != ENT_ARRAY) { fail("offset array li=%d not an array entry", li); return {}; }
        const std::vector<uint8_t>& src = arrStore_[e.storeId];
        // Re-yield from arrayIndex `ai` to the end of the source block, in elemSize units when matching.
        size_t startByte = (size_t)ai * e.entrySize;
        if (startByte > src.size()) { fail("offset array ai=%d oob", ai); return {}; }
        std::vector<uint8_t> out(src.begin() + startByte, src.end());
        count = (elemSize && (out.size() % elemSize) == 0) ? (uint32_t)(out.size() / elemSize)
                                                           : (e.entryCount > (uint32_t)ai ? e.entryCount - ai : 0);
        return out;
    }

    void fail(const char* fmt, ...) {
        if (!ok_) return; // keep first error
        ok_ = false;
        char buf[256];
        va_list ap; va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        err_ = buf;
    }

    const uint8_t* p_;
    size_t n_;
    size_t cur_ = 0;
    bool ok_ = true;
    std::string err_;
    std::vector<ListEntry> list_;
    std::vector<std::string> strStore_;
    std::vector<std::vector<uint8_t>> arrStore_;
};

} // namespace dumpsrc
