// ff_io.cpp — IW3 read + IW8 write + inspect, per ff_io.h.
#include "ff_io.h"
#include "fs_util.h"
#include "log.h"
#include "oodle.h"
#include "zlib_inflate.h"
#include "../iw8/iw8_ffheader.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace zt {

// ===================================================================================================
// IW3 read
// ===================================================================================================
bool iw3_parse(const std::vector<uint8_t>& file, Iw3Fastfile& out) {
    out = Iw3Fastfile{};
    if (file.size() < 12) {
        zt::err("iw3: file too small (%zu)", file.size());
        return false;
    }
    if (std::memcmp(file.data(), "IWffu100", 8) != 0) {
        zt::err("iw3: bad magic (expected IWffu100)");
        return false;
    }
    uint32_t ver;
    std::memcpy(&ver, file.data() + 8, 4);
    out.version = ver;
    if (ver != 5)
        zt::warn("iw3: version %u (expected 5 for CoD4) — continuing", ver);

    // zlib stream starts at 0x0C (first byte should be 0x78).
    const uint8_t* z = file.data() + 12;
    size_t zlen = file.size() - 12;
    if (zlen == 0 || z[0] != 0x78)
        zt::warn("iw3: zlib stream does not start with 0x78 (0x%02X)", z[0]);

    if (!inflate_all(z, zlen, out.zone)) {
        zt::err("iw3: zone inflate failed");
        return false;
    }
    zt::info("iw3: inflated zone %zu -> %zu bytes (ver %u)", zlen, out.zone.size(), out.version);
    return true;
}

bool iw3_read(const std::string& ffPath, Iw3Fastfile& out) {
    std::vector<uint8_t> file;
    if (!read_file(ffPath, file)) {
        zt::err("iw3: cannot read %s", ffPath.c_str());
        return false;
    }
    return iw3_parse(file, out);
}

// ===================================================================================================
// IW8 write
// ===================================================================================================
bool iw8_write(const std::string& outPath,
               const std::vector<uint8_t>& zoneBody,
               const Iw8WriteParams& params) {
    using namespace iw8ff;

    // The Oodle frame decompresses to the BODY bytes only; calc/zero-fill streams (stream 4 = dpvs) are
    // counted in blockSize[]/totalDecompressed for region RESERVATION but carry NO body bytes (the loader
    // memsets them at load). So XFile.size (the decode rawLen) = body len = totalDecompressed - calcSize.
    // When calcSize==0 (every zone today) this is identical to totalDecompressed -> byte-for-byte no change.
    if (params.calcSize > params.totalDecompressed ||
        params.totalDecompressed - params.calcSize != zoneBody.size() ||
        zoneBody.size() > UINT32_MAX - 0xA0) {
        zt::err("iw8: invalid body size or stream reservation for %s", outPath.c_str());
        return false;
    }
    const uint64_t bodyRawLen = params.totalDecompressed - params.calcSize;

    // 1) Codec the body.
    std::vector<uint8_t> body;
    Iw8Codec codec = params.codec;
    uint8_t dashCompress = 0;

    if (codec == Iw8Codec::Oodle) {
        if (!Oodle::load(params.oodlePath)) {
            zt::err("iw8: %s", Oodle::last_error().c_str());
            return false;
        }
        if (!Oodle::compress(zoneBody, body)) {
            zt::err("iw8: Oodle compress failed (%s)", Oodle::last_error().c_str());
            return false;
        }

        if (zoneBody.size() != bodyRawLen) {
            zt::err("iw8: %s body/XFile.size desync (%zu != %llu) — align padding not materialized "
                    "(totalDecompressed=%llu calcSize=%llu)",
                    outPath.c_str(), zoneBody.size(), (unsigned long long)bodyRawLen,
                    (unsigned long long)params.totalDecompressed,
                    (unsigned long long)params.calcSize);
            return false;
        }
        std::vector<uint8_t> rtCheck;
        if (!Oodle::decompress(body, zoneBody.size(), rtCheck) || rtCheck != zoneBody) {
            zt::err(
                "iw8: %s Oodle round-trip self-check FAILED (got %zu of %zu bytes) — not writing",
                outPath.c_str(), rtCheck.size(), zoneBody.size());
            return false;
        }
        dashCompress = 1;
    } else if (codec == Iw8Codec::Zlib) {
        if (!deflate_all(zoneBody, body, -1)) {
            zt::err("iw8: zlib deflate failed");
            return false;
        }
        dashCompress = 2;
    } else { // Stored
        body = zoneBody;
        dashCompress = 0;
    }

    // Replay uses an 0x88-byte header followed by an IWC resident stream.
    // Mode 1 is stored bytes. IWffc100 selects the native unsigned decoder;
    // mw120rproxy permits its initialization only for the selected custom package.
    // The legacy Oodle path below lacks full signed auth framing and is not used
    // by the Replay package command.
    const bool iwc = (codec == Iw8Codec::Stored);
    static const unsigned char kIwcStoredFrame[4] = {0x01, 0x49, 0x57,
                                                     0x43}; // modeByte=1 STORED, 'I','W','C'

    std::vector<uint8_t> resident; // the bytes after the 0x88 header (== residentPartSize)
    if (iwc) {
        resident.reserve(4 + zoneBody.size());
        resident.insert(resident.end(), kIwcStoredFrame, kIwcStoredFrame + 4);
        resident.insert(resident.end(), zoneBody.begin(),
                        zoneBody.end()); // STORED: decode output == zoneBody
    } else {
        if (body.size() < 0x70)
            body.resize(0x70, 0); // legacy short-resident guard
        resident = std::move(body);
    }
    const uint32_t residentSize = static_cast<uint32_t>(resident.size());

    // 3) Assemble DB_FFHeader.
    IW8_DB_FFHeader h{};
    std::memset(&h, 0, sizeof(h));
    std::memcpy(h.magic, iwc ? kMagicUnsec : kMagic, 8);
    h.headerVersion = kHeaderVersion;   // 11 (hard gate)
    h.xfileVersion = kXFileVersion;     // 0xFF7 for normal Replay fastfiles
    h.dashCompressBuild = dashCompress; // 1=Oodle / 0=stored / 2=zlib
    h.dashEncryptBuild = 0;
    h.transientFileType = params.transientFileType;
    h.residentPartSize = iwc ? residentSize : (0x18 + residentSize); // filesize - 0x88
    h.residentHash = 0; // db_validateloadhash off by default
    h.alwaysLoadedPartSize = static_cast<uint32_t>(bodyRawLen);
    h.xfileHeader.size =
        bodyRawLen; // decode rawLen = body bytes (== zoneBody.size(); excludes calc)
    h.xfileHeader.preloadWalkSize = 0;
    for (int i = 0; i < kNumStreams; ++i)
        h.xfileHeader.blockSize[i] = params.blockSize[i];
    if (!iwc && params.writeSigMagic)
        std::memcpy(h.xfileHeader.encryption.magic, kSigMagic, 8);

    // 4) Write. IWC-stored = 0x88 header proper (no EncryptionHeader) + the ?IWC resident stream at 0x88;
    //    legacy = full 0xA0 struct + body at 0xA0.
    std::string dir = path_dir(outPath);
    if (!dir.empty())
        mkdirs(dir);
    FILE* f = std::fopen(outPath.c_str(), "wb");
    if (!f) {
        zt::err("iw8: cannot open %s for write", outPath.c_str());
        return false;
    }
    const size_t headerBytes = iwc ? 0x88 : sizeof(h);
    std::fwrite(&h, 1, headerBytes, f);
    std::fwrite(resident.data(), 1, resident.size(), f);
    std::fclose(f);

    const char* codecName = iwc                          ? "stored-IWC"
                            : (codec == Iw8Codec::Oodle) ? "Oodle"
                            : (codec == Iw8Codec::Zlib)  ? "zlib"
                                                         : "stored";
    zt::info(
        "iw8: wrote %s : %zu bytes (header 0x%zX + resident %u, codec=%s, magic=%.8s, XFile.size=0x%llX)",
        outPath.c_str(), headerBytes + resident.size(), headerBytes, residentSize, codecName,
        h.magic, (unsigned long long)h.xfileHeader.size);
    return true;
}

// ===================================================================================================
// inspect
// ===================================================================================================
static void inspect_iw8(const std::vector<uint8_t>& file) {
    using namespace iw8ff;
    if (file.size() < 0x8C) {
        zt::err("iw8: file too small for header");
        return;
    }
    IW8_DB_FFHeader h{};
    std::memcpy(&h, file.data(), (std::min)(file.size(), sizeof(h)));
    std::printf("== IW8 fastfile (DB_FFHeader) ==\n");
    std::printf("  magic              : %.8s\n", h.magic);
    std::printf("  headerVersion      : %u %s\n", h.headerVersion,
                h.headerVersion == kHeaderVersion ? "(OK)" : "(MISMATCH!)");
    std::printf("  xfileVersion       : 0x%X\n", h.xfileVersion);
    std::printf("  dashCompressBuild  : %u (%s)\n", h.dashCompressBuild,
                h.dashCompressBuild == 0   ? "stored"
                : h.dashCompressBuild == 1 ? "Oodle"
                                           : "other");
    std::printf("  dashEncryptBuild   : %u\n", h.dashEncryptBuild);
    std::printf("  transientFileType  : %u\n", h.transientFileType);
    std::printf("  residentPartSize   : 0x%X (filesize-0x88=0x%llX)\n", h.residentPartSize,
                (unsigned long long)(file.size() - 0x88));
    std::printf("  residentHash       : 0x%X\n", h.residentHash);
    std::printf("  alwaysLoadedPartSize: 0x%X\n", h.alwaysLoadedPartSize);
    std::printf("  XFile.size         : 0x%llX (decompressed total)\n",
                (unsigned long long)h.xfileHeader.size);
    std::printf("  XFile.preloadWalk  : 0x%llX\n",
                (unsigned long long)h.xfileHeader.preloadWalkSize);
    std::printf("  XFile.blockSize[11]:");
    for (int i = 0; i < kNumStreams; ++i)
        std::printf(" [%d]=%llu", i, (unsigned long long)h.xfileHeader.blockSize[i]);
    std::printf("\n");
    std::printf("  EncryptionHeader   : %.8s\n", h.xfileHeader.encryption.magic);
}

static void inspect_iw3(const std::vector<uint8_t>& file) {
    uint32_t ver = 0;
    if (file.size() >= 12)
        std::memcpy(&ver, file.data() + 8, 4);
    std::printf("== IW3 fastfile (IWffu100) ==\n");
    std::printf("  magic              : %.8s\n", file.data());
    std::printf("  version            : %u %s\n", ver, ver == 5 ? "(CoD4)" : "");
    std::printf("  zlib stream @0x0C  : 0x%02X 0x%02X ...\n", file.size() > 12 ? file[12] : 0,
                file.size() > 13 ? file[13] : 0);
    std::printf("  filesize           : %zu bytes\n", file.size());
    Iw3Fastfile ff;
    if (iw3_parse(file, ff))
        std::printf("  inflated zone      : %zu bytes\n", ff.zone.size());
    else
        std::printf("  inflated zone      : (inflate failed)\n");
}

bool inspect_ff(const std::string& ffPath) {
    std::vector<uint8_t> file;
    if (!read_file(ffPath, file)) {
        zt::err("inspect: cannot read %s", ffPath.c_str());
        return false;
    }
    if (file.size() < 8) {
        zt::err("inspect: file too small");
        return false;
    }
    std::printf("file: %s\n", ffPath.c_str());
    if (std::memcmp(file.data(), "IWffa100", 8) == 0 ||
        std::memcmp(file.data(), "IWffc100", 8) == 0) {
        inspect_iw8(file);
        return true;
    }
    if (std::memcmp(file.data(), "IWffu100", 8) == 0) {
        inspect_iw3(file);
        return true;
    }
    zt::err("inspect: unrecognized magic '%.8s'", file.data());
    return false;
}

} // namespace zt
