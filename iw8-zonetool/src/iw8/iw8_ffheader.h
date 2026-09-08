// ffheader.h — MW2019 1.20 Replay DB_FFHeader container.
// The fields come from the installed 1.20 Replay stock files. The primary sample is
// zone/mp_shipment.ff. The converter must not use 1.24 values for this target.
#pragma once
#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

// EncryptionHeader (XFile + 0x68, file offset 0x88; size 20). It carries the secondary "IWffs100"
// marker. Stock 1.20 files have nonzero trailing auth bytes. The converter emits the marker and zeros
// until a live 1.20 test proves the exact trailing-auth rule.
struct IW8_EncryptionHeader {
    char magic[8];    // "IWffs100"
    uint8_t rest[12]; // signature/hash bytes (mp_shipment: 00 00 00 00 a1 95 50 14 49 4e 8f fe)
};
static_assert(sizeof(IW8_EncryptionHeader) == 20, "EncryptionHeader must be 20 bytes");

// XFile (DB_FFHeader + 0x20; size 128). Inner block-size header (dev struct).
struct IW8_XFile {
    uint64_t size;            // 0x00  decompressed total zone size (mp_shipment: 0x175FD16F)
    uint64_t preloadWalkSize; // 0x08  (mp_shipment: 0x87F3)
    uint64_t
        blockSize[11]; // 0x10  per-stream (DBStreamStart) decompressed sizes — IW8 has 11 streams
    IW8_EncryptionHeader encryption; // 0x68  (file 0x88) "IWffs100" + sig
    uint8_t _pad[4];                 // 0x7C  -> 0x80
};
static_assert(sizeof(IW8_XFile) == 128, "XFile must be 128 bytes");
static_assert(offsetof(IW8_XFile, blockSize) == 0x10, "blockSize@0x10");
static_assert(offsetof(IW8_XFile, encryption) == 0x68, "encryption@0x68");

struct IW8_DB_FFHeader {
    char magic[8];             // 0x00  "IWffa100"
    uint32_t headerVersion;    // 0x08  = 11  (the asserted HARD gate)
    uint32_t xfileVersion;     // 0x0C  = 0xFF7 (MW2019 1.20 Replay asset format)
    uint8_t dashCompressBuild; // 0x10  0 = stored, 1 = Oodle
    uint8_t dashEncryptBuild;  // 0x11  = 0
    uint8_t transientFileType; // 0x12  0 = base/resident zone, 1 = _tr transient
    uint8_t _pad13;            // 0x13
    uint32_t residentPartSize; // 0x14  filesize == 0x88 + residentPartSize
    uint32_t
        residentHash; // 0x18  XXH64(resident)-folded; 0 ok if db_validateloadhash off (default)
    uint32_t alwaysLoadedPartSize; // 0x1C
    IW8_XFile xfileHeader;         // 0x20  (128)
};
static_assert(sizeof(IW8_DB_FFHeader) == 0xA0, "DB_FFHeader must be 0xA0");
static_assert(offsetof(IW8_DB_FFHeader, residentPartSize) == 0x14, "residentPartSize@0x14");
static_assert(offsetof(IW8_DB_FFHeader, xfileHeader) == 0x20, "xfileHeader@0x20");

#pragma pack(pop)

namespace iw8ff {
// Values from the installed MW2019 1.20.4 Replay stock family. mp_shipment.ff, srv_mp_shipment.ff,
// and techsets_mp_shipment.ff all use this header version, XFile version, and stream count.
constexpr uint32_t kHeaderVersion = 11;
constexpr uint32_t kXFileVersion = 0xFF7;
constexpr int kNumStreams = 11;
constexpr uint32_t kResidentBase =
    0x88; // filesize = 0x88 + residentPartSize (header proper = 0x88)
inline constexpr char kMagic[9] = "IWffa100"; // 'a' = SECURED (isSecured=1 -> RSA/SHA auth ring)
inline constexpr char kMagicUnsec[9] =
    "IWffc100"; // 'c' = UNSECURED (isSecured=0 -> the DIRECT ?IWC decode path)
inline constexpr char kSigMagic[9] = "IWffs100";
}
