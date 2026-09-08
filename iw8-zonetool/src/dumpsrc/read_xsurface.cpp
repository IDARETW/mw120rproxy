// read_xsurface.cpp — Stage-A reader: ZoneTool (IW5) XSurface dump (.xse) -> in-mem XseFile.
// =================================================================================================
// NEW DUMP->IW8 PATH (Tier2 xsurface). Parses XSurface\<name>.xse via the BinaryDumper reader
// (bindump.h::BinReader), in the EXACT read order of IW5 IXSurface::parse
// (zonetool-develop/src/IW5/Assets/XSurface.cpp:15-83) — see reference/IW5_DUMP_FORMAT.md §5.
//
// Read order (mirrors parse() one-for-one):
//   read_array<ModelSurface>()   -> 36-byte blob; xSurficiesCount = short @ +8; partBits @ +12 (6 ints)
//   read_string()                -> ModelSurface.name
//   for i in 0..xSurficiesCount-1:
//     4x read_int  -> tileMode, deformed, baseTriIndex, baseVertIndex
//     6x read_int  -> partBits[0..5]
//     read_array<XSurfaceVertexInfo>(12) -> vertCount[4] @ +0 (the trailing ptr is stale/ignored)
//     read_array<u16>             -> vertsBlend (count = vc0 + vc1*3 + vc2*5 + vc3*7)
//     read_int                    -> vertCount
//     read_array<GfxPackedVertex>(32) -> verticies
//     read_int                    -> triCount
//     read_array<Face>(6)         -> triIndices (triCount*3 u16)
//     read_int                    -> vertListCount
//     read_array<XRigidVertList>(12) -> rigidVertLists
//     for each vert run: if its stale collisionTree ptr (@ +8 of the 12-byte elem) != 0,
//                        read_array<XSurfaceCollisionTree>(40) then optional leafs/nodes  (SKIPPED-READ:
//                        we consume the stream to stay in sync but DISCARD the collision accel — it is
//                        not load-required for the IW8 prototype geometry).
//
// IW5 32-bit struct sizes used as elemSize (Structs.hpp / IW5_DUMP_FORMAT §5): ModelSurface=36,
// XSurfaceVertexInfo=12, GfxPackedVertex=32, Face=6, XRigidVertList=12, XSurfaceCollisionTree=40,
// XSurfaceCollisionNode=16, XSurfaceCollisionLeaf=2.
//
// Fully offline: pure bytes-in, no game memory. On ANY desync BinReader sets !ok() and we bail with
// loaded=false + parseError (the writer then emits a load-safe minimal XModelSurfs).
// =================================================================================================
#include "xse_dump.h"
#include "bindump.h"
#include "common/fs_util.h"
#include "common/log.h"
#include <cstring>

namespace dumpsrc {

// IW5 32-bit struct sizes (the array element strides on disk).
namespace xsesz {
    constexpr size_t ModelSurface          = 36;
    constexpr size_t XSurfaceVertexInfo    = 12;  // short[4] + ptr(4)
    constexpr size_t GfxPackedVertex       = 32;  // float[3]+float+u32*4
    constexpr size_t Face                  = 6;   // u16 v1,v2,v3
    constexpr size_t XRigidVertList        = 12;  // u16*4 + collisionTree*(4)
    constexpr size_t XSurfaceCollisionTree = 40;  // float[3]+float[3]+u32+ptr+u32+ptr
    constexpr size_t XSurfaceCollisionNode = 16;
    constexpr size_t XSurfaceCollisionLeaf = 2;
    // field offsets inside the raw element bytes
    constexpr size_t ModelSurface_xSurficiesCount = 8;   // short @ +8
    constexpr size_t ModelSurface_partBits        = 12;  // int[6] @ +12
    constexpr size_t XRigidVertList_collisionTree = 8;   // stale ptr @ +8 (presence flag)
    constexpr size_t XSurfaceCollisionTree_nodeCount = 24; // u32 @ +24 (after 6 floats)
    constexpr size_t XSurfaceCollisionTree_leafCount = 32; // u32 @ +32 (after nodeCount + nodes ptr)
    // and the stale presence pointers inside the 40-byte tree blob:
    constexpr size_t XSurfaceCollisionTree_nodesPtr  = 28; // ptr @ +28
    constexpr size_t XSurfaceCollisionTree_leafsPtr  = 36; // ptr @ +36
}

XseFile parseXse(const std::vector<uint8_t>& bytes, const std::string& surfaceName) {
    XseFile out;
    if (bytes.empty()) { out.parseError = "empty .xse"; return out; }

    BinReader r(bytes);

    // ---- read_array<ModelSurface>() : the 36-byte header blob ------------------------------------
    uint32_t msCount = 0;
    std::vector<uint8_t> ms = r.read_array(xsesz::ModelSurface, msCount);
    if (!r.ok() || msCount != 1 || ms.size() < xsesz::ModelSurface) {
        out.parseError = r.ok() ? "ModelSurface count!=1" : r.error();
        return out;
    }
    const int16_t xSurficiesCount = BinReader::geti16(ms, xsesz::ModelSurface_xSurficiesCount);
    for (int k = 0; k < 6; ++k)
        out.modelSurfPartBits[k] = BinReader::geti32(ms, xsesz::ModelSurface_partBits + (size_t)k * 4);

    // ---- read_string() : ModelSurface.name ------------------------------------------------------
    out.name = r.read_string();
    if (!r.ok()) { out.parseError = r.error(); return out; }
    if (!surfaceName.empty() && !out.name.empty() && out.name != surfaceName) {
        zt::warn("dumpsrc xse '%s': name in file is '%s' (mismatch; using file name)",
                 surfaceName.c_str(), out.name.c_str());
    }

    if (xSurficiesCount < 0) { out.parseError = "negative xSurficiesCount"; return out; }
    out.surfaces.resize((size_t)xSurficiesCount);

    // ---- per-surface field-interleaved body ------------------------------------------------------
    for (int i = 0; i < xSurficiesCount && r.ok(); ++i) {
        XseSurface& s = out.surfaces[(size_t)i];

        s.tileMode      = r.read_int();
        s.deformed      = r.read_int();
        s.baseTriIndex  = r.read_int();
        s.baseVertIndex = r.read_int();
        for (int j = 0; j < 6; ++j) s.partBits[j] = r.read_int();
        if (!r.ok()) { out.parseError = r.error(); return out; }

        // read_array<XSurfaceVertexInfo>() — 1 elem, 12 B. vertCount[4] @ +0 (the trailing ptr is stale).
        uint32_t viCount = 0;
        std::vector<uint8_t> vi = r.read_array(xsesz::XSurfaceVertexInfo, viCount);
        if (!r.ok()) { out.parseError = r.error(); return out; }
        if (viCount >= 1 && vi.size() >= 8) {
            for (int k = 0; k < 4; ++k)
                s.vertBlendCounts[k] = BinReader::geti16(vi, (size_t)k * 2);
        }

        // read_array<unsigned short>() -> vertsBlend. count = vc0 + vc1*3 + vc2*5 + vc3*7.
        uint32_t vbCount = 0;
        std::vector<uint8_t> vb = r.read_array(2 /*u16*/, vbCount);
        if (!r.ok()) { out.parseError = r.error(); return out; }
        s.vertsBlend.resize(vbCount);
        for (uint32_t k = 0; k < vbCount; ++k)
            s.vertsBlend[k] = BinReader::getu16(vb, (size_t)k * 2);

        // read_int vertCount; read_array<GfxPackedVertex>() -> verticies.
        s.vertCount = (uint32_t)r.read_int();
        uint32_t vCount = 0;
        std::vector<uint8_t> vbuf = r.read_array(xsesz::GfxPackedVertex, vCount);
        if (!r.ok()) { out.parseError = r.error(); return out; }
        s.verticies.resize(vCount);
        for (uint32_t v = 0; v < vCount; ++v) {
            const size_t base = (size_t)v * xsesz::GfxPackedVertex;
            XseVertex& gv = s.verticies[v];
            gv.xyz[0]       = BinReader::getf(vbuf, base + 0);
            gv.xyz[1]       = BinReader::getf(vbuf, base + 4);
            gv.xyz[2]       = BinReader::getf(vbuf, base + 8);
            gv.binormalSign = BinReader::getf(vbuf, base + 12);
            gv.color        = BinReader::getu32(vbuf, base + 16);
            gv.texCoord     = BinReader::getu32(vbuf, base + 20);
            gv.normal       = BinReader::getu32(vbuf, base + 24);
            gv.tangent      = BinReader::getu32(vbuf, base + 28);
        }

        // read_int triCount; read_array<Face>() -> triIndices (triCount*3 u16).
        s.triCount = (uint32_t)r.read_int();
        uint32_t faceCount = 0;
        std::vector<uint8_t> fbuf = r.read_array(xsesz::Face, faceCount);
        if (!r.ok()) { out.parseError = r.error(); return out; }
        s.triIndices.resize((size_t)faceCount * 3);
        for (uint32_t f = 0; f < faceCount; ++f) {
            const size_t base = (size_t)f * xsesz::Face;
            s.triIndices[(size_t)f * 3 + 0] = BinReader::getu16(fbuf, base + 0);
            s.triIndices[(size_t)f * 3 + 1] = BinReader::getu16(fbuf, base + 2);
            s.triIndices[(size_t)f * 3 + 2] = BinReader::getu16(fbuf, base + 4);
        }

        // read_int vertListCount; read_array<XRigidVertList>() -> rigidVertLists.
        s.vertListCount = (uint32_t)r.read_int();
        uint32_t rvlCount = 0;
        std::vector<uint8_t> rbuf = r.read_array(xsesz::XRigidVertList, rvlCount);
        if (!r.ok()) { out.parseError = r.error(); return out; }
        s.rigidVertLists.resize(rvlCount);
        for (uint32_t k = 0; k < rvlCount; ++k) {
            const size_t base = (size_t)k * xsesz::XRigidVertList;
            XseRigidVertList& rv = s.rigidVertLists[k];
            rv.boneOffset = BinReader::getu16(rbuf, base + 0);
            rv.vertCount  = BinReader::getu16(rbuf, base + 2);
            rv.triOffset  = BinReader::getu16(rbuf, base + 4);
            rv.triCount   = BinReader::getu16(rbuf, base + 6);

            // collisionTree presence = the stale ptr @ +8. If nonzero, the dump emitted the tree (and
            // optionally its leafs/nodes). We MUST consume them to stay stream-synced, then discard.
            const uint32_t ctPtr = BinReader::getu32(rbuf, base + xsesz::XRigidVertList_collisionTree);
            if (ctPtr) {
                uint32_t ctCount = 0;
                std::vector<uint8_t> ct = r.read_array(xsesz::XSurfaceCollisionTree, ctCount);
                if (!r.ok()) { out.parseError = r.error(); return out; }
                if (ctCount >= 1 && ct.size() >= xsesz::XSurfaceCollisionTree) {
                    const uint32_t leafsPtr = BinReader::getu32(ct, xsesz::XSurfaceCollisionTree_leafsPtr);
                    const uint32_t nodesPtr = BinReader::getu32(ct, xsesz::XSurfaceCollisionTree_nodesPtr);
                    // dump() writes leafs THEN nodes (XSurface.cpp:256-266 order: leafs, then nodes).
                    if (leafsPtr) { uint32_t c; r.read_array(xsesz::XSurfaceCollisionLeaf, c); }
                    if (nodesPtr) { uint32_t c; r.read_array(xsesz::XSurfaceCollisionNode, c); }
                    if (!r.ok()) { out.parseError = r.error(); return out; }
                }
            }
        }
    }

    if (!r.ok()) { out.parseError = r.error(); return out; }

    // Sanity: a clean parse consumes the whole file (the .xse has no trailing data). A leftover tail
    // means a desync we didn't catch — treat as a soft warning (don't fail; the data we read is valid).
    if (r.remaining() != 0) {
        zt::warn("dumpsrc xse '%s': %zu trailing byte(s) after parse (read order may be incomplete)",
                 out.name.c_str(), r.remaining());
    }

    out.loaded = true;
    return out;
}

// =================================================================================================
// loadXseFile — read <dumpDir>/XSurface/<name>.xse off disk and parse it. This is the entry the IW8
// writer + the dump CLI use. It does NOT define DumpSource::loadXSurface (that stub lives in
// dump_source.cpp and stays loaded=false) — keeping symbol ownership clean and avoiding an ODR clash.
// Fully offline: pure file-in, no game memory.
// =================================================================================================
XseFile loadXseFile(const std::string& dumpDir, const std::string& surfaceName) {
    XseFile f;
    std::string p = zt::path_join(zt::path_join(dumpDir, "XSurface"), surfaceName + ".xse");
    std::vector<uint8_t> bytes;
    if (!zt::read_file(p, bytes)) {
        f.parseError = "no .xse at " + p;
        zt::warn("dumpsrc: %s", f.parseError.c_str());
        return f; // loaded=false
    }
    f = parseXse(bytes, surfaceName);
    if (!f.loaded)
        zt::err("dumpsrc xse '%s': parse failed: %s", surfaceName.c_str(), f.parseError.c_str());
    return f;
}

} // namespace dumpsrc
