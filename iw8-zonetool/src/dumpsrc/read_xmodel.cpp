

#include "xmodel_dump.h"
#include "bindump.h"
#include "common/fs_util.h"
#include "common/log.h"
#include <cstring>

namespace dumpsrc {

namespace iw5xm {
constexpr size_t XModel = 308;        // total (Structs.hpp:1009 "total size 308")
constexpr size_t shortSz = 2;         // boneNames element (read_array<short>)
constexpr size_t u8Sz = 1;            // parentList / partClassification element
constexpr size_t XModelAngle = 8;     // tagAngles
constexpr size_t XModelTagPos = 12;   // tagPositions
constexpr size_t DObjAnimMat = 32;    // animMatrix
constexpr size_t XBoneInfo = 28;      // boneInfo
constexpr size_t MaterialPtr = 4;     // materials (pointer array, raw)
constexpr size_t XModelCollSurf = 56; // colSurf
constexpr size_t XModelCollTri = 48;  // colSurf[i].tris

// header field byte offsets inside the 308-byte XModel blob
constexpr size_t off_numBones = 4;
constexpr size_t off_numRoot = 5;
constexpr size_t off_numSurfaces = 6;
constexpr size_t off_lodRampType = 7;
constexpr size_t off_scale = 8;
constexpr size_t off_lods = 64; // lods[4], 44 bytes each
constexpr size_t XSurfaceLod = 44;
constexpr size_t off_numLods = 241;
constexpr size_t off_collLod = 242;
constexpr size_t off_flags = 243;
constexpr size_t off_numColSurfs = 248;
constexpr size_t off_contents = 252; // i32
constexpr size_t off_radius = 260;   // f32
constexpr size_t off_bounds = 264;   // Bounds{midPoint f32[3], halfSize f32[3]} = 24B
// XSurfaceLod field offsets
constexpr size_t lod_dist = 0;      // f32
constexpr size_t lod_numSurf = 4;   // i16
constexpr size_t lod_surfIndex = 6; // i16
constexpr size_t lod_partBits = 12; // u32[6]
}

bool parseXModel(const std::vector<uint8_t>& bytes,
                 const std::string& nameHint,
                 XModelDumpFull& out) {
    using zt::warn;
    out = XModelDumpFull{};
    out.name = nameHint;
    if (bytes.empty()) {
        warn("read_xmodel: empty .xme6 for '%s'", nameHint.c_str());
        return false;
    }

    BinReader r(bytes);

    // 1) header blob = read_single<XModel> (= read_array of 1 element, 308 bytes)
    uint32_t hcnt = 0;
    std::vector<uint8_t> hdr = r.read_array(iw5xm::XModel, hcnt);
    if (!r.ok() || hcnt != 1 || hdr.size() < iw5xm::XModel) {
        warn("read_xmodel: '%s' header read failed (%s, cnt=%u sz=%zu)", nameHint.c_str(),
             r.error().c_str(), hcnt, hdr.size());
        return false;
    }

    out.numBones = BinReader::getu8(hdr, iw5xm::off_numBones);
    out.numRootBones = BinReader::getu8(hdr, iw5xm::off_numRoot);
    out.numSurfaces = BinReader::getu8(hdr, iw5xm::off_numSurfaces);
    out.lodRampType = BinReader::getu8(hdr, iw5xm::off_lodRampType);
    out.scale = BinReader::getf(hdr, iw5xm::off_scale);
    out.numLods = BinReader::getu8(hdr, iw5xm::off_numLods);
    out.collLod = (int8_t)BinReader::getu8(hdr, iw5xm::off_collLod);
    out.flags = BinReader::getu8(hdr, iw5xm::off_flags);
    out.numColSurfs = BinReader::geti32(hdr, iw5xm::off_numColSurfs);
    out.contents = BinReader::geti32(hdr, iw5xm::off_contents);
    out.radius = BinReader::getf(hdr, iw5xm::off_radius);
    for (int k = 0; k < 3; ++k) {
        out.boundsMid[k] = BinReader::getf(hdr, iw5xm::off_bounds + (size_t)k * 4);
        out.boundsHalf[k] = BinReader::getf(hdr, iw5xm::off_bounds + 12 + (size_t)k * 4);
    }

    if (out.scale == 0.f)
        out.scale = 1.f;
    if (out.numLods > 4)
        out.numLods = 4; // IW5 XModel has exactly lods[4]
    const int animBones = (out.numBones > out.numRootBones) ? (out.numBones - out.numRootBones) : 0;

    // per-LOD scalars from the header blob (the surface NAMES come later via read_asset)
    for (int i = 0; i < 4; ++i) {
        const size_t lo = iw5xm::off_lods + (size_t)i * iw5xm::XSurfaceLod;
        XModelLodDump& L = out.lods[i];
        L.dist = BinReader::getf(hdr, lo + iw5xm::lod_dist);
        L.numSurfacesInLod = BinReader::getu16(hdr, lo + iw5xm::lod_numSurf);
        L.surfIndex = BinReader::getu16(hdr, lo + iw5xm::lod_surfIndex);
        for (int k = 0; k < 6; ++k)
            L.partBits[k] = BinReader::getu32(hdr, lo + iw5xm::lod_partBits + (size_t)k * 4);
    }

    // 2) name (read_string overwrites the stale ptr in the blob)
    std::string realName = r.read_string();
    if (r.ok() && !realName.empty())
        out.name = realName;
    out.loaded = r.ok(); // minimum success once header+name parsed
    if (!r.ok()) {
        warn("read_xmodel: '%s' name read failed: %s", nameHint.c_str(), r.error().c_str());
        return false;
    }

    // 3) boneNames: array<short> then numBones×read_string
    uint32_t c = 0;
    r.read_array(iw5xm::shortSz, c); // the (stale) short[] handles; names follow
    out.boneNames.reserve(out.numBones);
    for (int i = 0; i < out.numBones && r.ok(); ++i)
        out.boneNames.push_back(r.read_string());

    // 4) parentList / tagAngles / tagPositions (count = animBones)
    r.read_array(iw5xm::u8Sz, c);         // parentList
    r.read_array(iw5xm::XModelAngle, c);  // tagAngles
    r.read_array(iw5xm::XModelTagPos, c); // tagPositions

    // 5) partClassification (numBones) / animMatrix (numBones) / boneInfo (numBones)
    r.read_array(iw5xm::u8Sz, c);        // partClassification
    r.read_array(iw5xm::DObjAnimMat, c); // animMatrix
    r.read_array(iw5xm::XBoneInfo, c);   // boneInfo

    // 6) materials: array<Material*> (raw ptrs) then numSurfaces×read_asset<Material>
    r.read_array(iw5xm::MaterialPtr, c); // the (stale) Material*[] ; real names follow as assets
    out.materials.reserve(out.numSurfaces);
    for (int i = 0; i < out.numSurfaces && r.ok(); ++i) {
        bool wasNull = false;
        std::string m = r.read_asset(&wasNull);
        out.materials.push_back(m); // OFFSET-deduped -> resolves to a real name; "" if null
    }

    // 7) lods[i].surfaces: numLods×read_asset<ModelSurface> -> the XModelSurfs link name
    for (int i = 0; i < out.numLods && r.ok(); ++i) {
        bool wasNull = false;
        std::string s = r.read_asset(&wasNull);
        if (i < 4)
            out.lods[i].surfsName = s;
    }

    // 8) colSurf: array<XModelCollSurf_s> then per-col tris array
    r.read_array(iw5xm::XModelCollSurf, c);
    for (int i = 0; i < out.numColSurfs && r.ok(); ++i)
        r.read_array(iw5xm::XModelCollTri, c);

    // 9) sub-assets: physPreset, physCollmap (by name; may be null)
    {
        bool wasNull = false;
        out.physPresetName = r.read_asset(&wasNull);
        out.physCollmapName = r.read_asset(&wasNull);
    }

    // clean == the walk consumed the whole stream with no desync
    out.clean = r.ok() && r.eof();
    if (!out.clean) {
        // The header + name + LOD links are still usable for a load-safe XModel; warn and proceed.
        warn("read_xmodel: '%s' walk did not consume cleanly (%s, pos=%zu/%zu) — header+links kept",
             out.name.c_str(), r.ok() ? "trailing bytes" : r.error().c_str(), r.pos(),
             bytes.size());
    }
    (void)animBones;
    return out.loaded;
}

bool readXModel(const std::string& dumpDir, const std::string& name, XModelDumpFull& out) {
    using zt::warn;
    std::string path = zt::path_join(dumpDir, "XModel/" + name + ".xme6");
    std::vector<uint8_t> bytes;
    if (!zt::read_file(path, bytes)) {
        warn("read_xmodel: cannot open %s", path.c_str());
        out = XModelDumpFull{};
        out.name = name;
        return false;
    }
    return parseXModel(bytes, name, out);
}

} // namespace dumpsrc
