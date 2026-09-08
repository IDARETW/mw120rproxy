

#pragma once
#include "iw3_zone.h"
#include <cstdint>
#include <string>
#include <vector>

namespace iw3mtl {

// IW3 Material serialized field offsets (bytes). See header comment.
namespace off {
constexpr size_t name = 0x00;            // u32 ptr
constexpr size_t gameFlags = 0x04;       // u8
constexpr size_t sortKey = 0x05;         // u8
constexpr size_t atlasRowCount = 0x06;   // u8
constexpr size_t atlasColCount = 0x07;   // u8
constexpr size_t surfaceTypeBits = 0x10; // u32
constexpr size_t animationX = 0x16;      // u8
constexpr size_t animationY = 0x17;      // u8
constexpr size_t numMaps = 0x3A;         // u8
constexpr size_t constantCount = 0x3B;   // u8
constexpr size_t stateBitsCount = 0x3C;  // u8
constexpr size_t stateFlags = 0x3D;      // u8
constexpr size_t cameraRegion = 0x3E;    // u8
constexpr size_t techniqueSet = 0x40;    // u32 ptr
constexpr size_t maps = 0x44;            // u32 ptr -> MaterialTextureDef[numMaps]
constexpr size_t constantTable = 0x48;   // u32 ptr -> MaterialConstantDef[constantCount]
constexpr size_t stateMap = 0x4C;        // u32 ptr -> GfxStateBits[stateBitsCount]
constexpr size_t SIZE = 0x50;

// MaterialTextureDef (0x0C)
constexpr size_t td_typeHash = 0x00;    // u32
constexpr size_t td_firstChar = 0x04;   // i8
constexpr size_t td_secondLast = 0x05;  // i8
constexpr size_t td_sampleState = 0x06; // i8
constexpr size_t td_semantic = 0x07;    // i8
constexpr size_t td_image = 0x08;       // u32 ptr -> GfxImage (or water_t* if semantic==0xB)
constexpr size_t TD_SIZE = 0x0C;

// MaterialConstantDef (0x20)
constexpr size_t cd_nameHash = 0x00; // u32
constexpr size_t cd_name = 0x04;     // char[12]
constexpr size_t cd_literal = 0x10;  // float[4]
constexpr size_t CD_SIZE = 0x20;

// GfxStateBits (0x08): loadBits[2]
constexpr size_t SB_SIZE = 0x08;

// GfxImage.name (ptr) offset within GfxImage (0x24 struct)
constexpr size_t img_name = 0x20;

}

// Read `n` bytes from the inflated zone at ABSOLUTE offset `abs` into `dst`. Returns false on OOB.
bool readAt(iw3::LoadCtx& lc, size_t abs, void* dst, size_t n);

// Read a scalar of type T at ABSOLUTE offset `abs`. Returns false on OOB (leaves v unspecified).
template <typename T> bool readScalarAt(iw3::LoadCtx& lc, size_t abs, T& v) {
    return readAt(lc, abs, &v, sizeof(T));
}

// Read a NUL-terminated string at ABSOLUTE offset `abs` (bounded scan). Empty on OOB/0-offset.
std::string readCStrAt(iw3::LoadCtx& lc, size_t abs);

// Resolve a 32-bit IW3 pointer field stored at ABSOLUTE offset `ptrFieldAbs` to a target absolute
// offset. Sets *isNull when the stored value is 0; sets *isFollows when it is 0xFFFFFFFF (the data
// follows inline at the cursor — NOT used for the cross-ref offsets the reader walks). Returns npos for
// null. For follows, returns the CURRENT cursor pos (callers that follow inline must position the
// cursor; the material reader only walks already-serialized cross-refs + the inline name, handled
// explicitly). Most material sub-pointers are packed offsets aliasing other streams.
size_t resolvePtrAt(iw3::LoadCtx& lc,
                    size_t ptrFieldAbs,
                    bool* isNull = nullptr,
                    bool* isFollows = nullptr);

// Given a resolved GfxImage base offset, read its name string (GfxImage.name @0x20).
std::string readImageName(iw3::LoadCtx& lc, size_t imageBaseAbs);

} // namespace iw3mtl
