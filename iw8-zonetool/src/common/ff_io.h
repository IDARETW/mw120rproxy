// ff_io.h — fastfile container codecs.
//   IW3 read : "IWffu100" + u32 ver(==5) + zlib stream -> inflate -> flat zone bytes.
//   IW8 write: assembled zone-stream bytes + per-stream blockSize[11] + decompressed total ->
//              DB_FFHeader (iw8_ffheader.h verbatim) + body codec (Oodle default / stored / zlib) ->
//              the .ff file on disk.
//   inspect  : detect IW3 vs IW8 by magic and print the header for the 'inspect' CLI.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zt {

// ---- IW3 fastfile (CoD4) -------------------------------------------------------------------------
struct Iw3Fastfile {
    uint32_t              version = 0;   // expect 5 (CoD4)
    std::vector<uint8_t>  zone;          // the inflated, flat 32-bit zone blob
};

// Parse an IW3 .ff from disk (read file, validate magic+version, inflate the zlib stream).
// Returns false on any failure (bad magic, wrong version, inflate error).
bool iw3_read(const std::string& ffPath, Iw3Fastfile& out);
// Same but from an already-read buffer.
bool iw3_parse(const std::vector<uint8_t>& file, Iw3Fastfile& out);

// ---- IW8 fastfile (MW2019 1.24) write ------------------------------------------------------------
enum class Iw8Codec { Oodle, Stored, Zlib };

struct Iw8WriteParams {
    Iw8Codec  codec       = Iw8Codec::Oodle;
    uint64_t  blockSize[11] = {0};   // per-stream region sizes (from the ZoneWriter; INCLUDES calc bytes)
    uint64_t  totalDecompressed = 0; // sum of blockSize[] (region total; == body len ONLY when calcSize==0)
    uint64_t  calcSize = 0;          // bytes reserved via reserveCalc (stream 4) — counted in blockSize[]
                                     // but NOT present in the body. XFile.size = totalDecompressed - calcSize.
    uint8_t   transientFileType = 0; // 0 = base/resident zone
    bool      writeSigMagic = true;  // stamp "IWffs100" @0x88 (shipped .ff carry it)
    std::string oodlePath;           // optional --oodle override for the codec
};

// Write an IW8 .ff: wrap `zoneBody` (the decompressed zone-stream bytes) in DB_FFHeader, compress per
// params.codec, and write to outPath. Returns false on failure (e.g. Oodle requested but unavailable).
bool iw8_write(const std::string& outPath, const std::vector<uint8_t>& zoneBody,
               const Iw8WriteParams& params);

// ---- inspect (debug) -----------------------------------------------------------------------------
// Detect a .ff's container (IW3 "IWffu100" / IW8 "IWffa100") and print its header fields to stdout.
// Returns false if the file is unreadable or the magic is unrecognized.
bool inspect_ff(const std::string& ffPath);

} // namespace zt
