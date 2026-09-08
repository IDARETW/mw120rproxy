

#pragma once
#include <cstdint>

// Pull in the iw8sz:: g_assetSizes constants (XMODELSURFS/XMODEL/MATERIAL/IMAGE) for the dual
// size-invariant static_asserts. iw8_structs.h restores default packing (pop) before its include
// guard ends, so the natural-alignment structs below are unaffected by its pack(1) region.
#include "iw8/iw8_structs.h"

namespace iw8_focus {
// ---- shared scalar aliases (match the dev PDB types) ----------------------------------------
using scr_string_t = uint32_t; // [PDB] script-string handle (u32)
struct vec2_t {
    float v[2];
};
struct vec3_t {
    float v[3];
};
struct vec4_t {
    float v[4];
}; // [PDB] size 0x10
struct Bounds {
    vec3_t midPoint;
    vec3_t halfSize;
}; // [PDB] 0x18

// forward decls
struct GfxImage;
struct Material;
struct MaterialTechniqueSet;
struct GfxImageFallback;
struct XModelSurfs;
struct XSurface;
struct XSurfaceShared;
struct XModelLodInfo;

// =============================================================================================
//  XPakEntryInfo  [PDB] 0x20 — the streamed-asset locator (GfxImageStreamData & XModelSurfs.
//  xpakEntry). All scalar; NO zone pointers. Serialize as 32 raw bytes.
// =============================================================================================
struct XPakEntryInfo {
    uint64_t key;      // 0x00
    int64_t offset;    // 0x08
    uint64_t size;     // 0x10
    uint64_t bitfield; // 0x18  xpakIndex:8, compressed:1, valid:1, adjacentLeftType:3,
                       //       adjacentRightType:3, adjacentLeft:19, adjacentRight:19, pad:10
};
static_assert(sizeof(XPakEntryInfo) == 0x20, "XPakEntryInfo");

// =============================================================================================
//  GfxImageStreamData  [PDB] 0x28 — one per mip-stream group (GfxImage.streams[4])
// =============================================================================================
struct GfxImageStreamData {
    XPakEntryInfo xpakEntry;    // 0x00  (0x20)  — scalar
    uint32_t levelCountAndSize; // 0x20  GfxImageStreamLevelCountAndSize (packed u32)
    uint16_t width;             // 0x24
    uint16_t height;            // 0x26
};
static_assert(sizeof(GfxImageStreamData) == 0x28, "GfxImageStreamData");

// =============================================================================================
//  GfxImageFallback  [PDB] 0x10 — low-res inline fallback pixels (pointed to by image.fallback)
// =============================================================================================
struct GfxImageFallback {
    uint8_t* pixels; // 0x00  //PTR  -> raw pixel bytes (size = .size)
    uint32_t size;   // 0x08  //CNT
    uint16_t width;  // 0x0C
    uint16_t height; // 0x0E
};
static_assert(sizeof(GfxImageFallback) == 0x10, "GfxImageFallback");

struct GfxImage {
    const char* name;            // 0x00  //PTR  XString
    uint8_t* packedAtlasData;    // 0x08  //PTR  (atlas blob; size = Material-side, often null)
    uint32_t textureId;          // 0x10  GfxTextureId (runtime; write 0)
    uint32_t format;             // 0x14  GfxPixelFormat (enum)
    uint32_t flags;              // 0x18  GfxImageFlags
    uint32_t totalSize;          // 0x1C
    uint32_t semanticSpecific;   // 0x20  GfxImageSemanticSpecific (union/u32)
    uint16_t width;              // 0x24
    uint16_t height;             // 0x26
    uint16_t depth;              // 0x28
    uint16_t numElements;        // 0x2A
    uint16_t atlasInfo;          // 0x2C  GfxImageAtlasInfo (packed u16)
    uint8_t semantic;            // 0x2E  TextureSemantic
    uint8_t category;            // 0x2F  GfxImageCategory
    uint8_t levelCount;          // 0x30
    uint8_t streamedPartCount;   // 0x31
    uint8_t decalAtlasIndex;     // 0x32
    int8_t freqDomainMetricBias; // 0x33
    // 0x34..0x37 implicit pad (8-align for streams[])
    GfxImageStreamData streams[4]; // 0x38  (0xA0)  — inline scalar array
    GfxImageFallback* fallback;

    void* pixels; // 0xE0  GfxImagePixels (8B) //PTR(runtime)
};
static_assert(sizeof(GfxImage) == 0xE8, "GfxImage size (pinned 0xE8)");
static_assert(sizeof(GfxImage) == iw8sz::IMAGE, "GfxImage size must equal g_assetSizes[19]");

// =============================================================================================
//  MaterialTextureDef  [PDB] 0x10 — one per bound image (Material.textureTable[textureCount])
//  NOTE: IW8 dev layout is the SIMPLE form {index, image}, NOT the H1/MWR nameHash form.
// =============================================================================================
struct MaterialTextureDef {
    uint8_t index; // 0x00  (sampler/dest slot)
    // 0x01..0x07 pad
    GfxImage* image; // 0x08  //PTR  -> bound GfxImage
};
static_assert(sizeof(MaterialTextureDef) == 0x10, "MaterialTextureDef");

// =============================================================================================
//  MaterialConstantDef  [PDB] fields + [LOAD] stride. Load_MaterialConstantDefArray does
//  Load_Stream(.., 20*count) => ARRAY STRIDE = 20 (0x14), tightly packed. (H1's 0x20 is WRONG
//  for IW8.)  All scalar; NO zone pointers.
//  DISCREPANCY: under C++ natural alignment the vec4_t literal at 0x04 makes the struct sizeof
//  0x14 anyway (4-byte align — no tail pad), so the disk stride 0x14 holds. Asserted below.
// =============================================================================================
struct MaterialConstantDef {
    uint8_t index; // 0x00
    // 0x01..0x03 pad
    vec4_t literal; // 0x04  (16 bytes)
};
static_assert(sizeof(MaterialConstantDef) == 0x14, "MaterialConstantDef stride must be 20 (0x14)");

// =============================================================================================
//  GfxStateBits  [PDB] 0x10 — two 64-bit packed render-state words. All scalar.
// =============================================================================================
struct GfxStateBits {
    uint64_t otherBits; // 0x00  GfxOtherStateBits
    uint64_t blendBits; // 0x08  GfxBlendStateBits
};
static_assert(sizeof(GfxStateBits) == 0x10, "GfxStateBits");

struct Material {
    const char* name;                // 0x00  //PTR  XString
    uint32_t contents;               // 0x08
    uint32_t surfaceFlags;           // 0x0C
    float maxDisplacement;           // 0x10
    uint32_t materialType;           // 0x14  MaterialGeometryType
    uint8_t cameraRegion;            // 0x18
    uint8_t sortKey;                 // 0x19
    uint16_t _u7;                    // 0x1A  ($-anon u16, flags)
    uint8_t textureCount;            // 0x1C  //CNT -> textureTable
    uint8_t constantCount;           // 0x1D  //CNT -> constantTable
    uint8_t constantBufferCount;     // 0x1E  //CNT -> constantBufferTable
    uint8_t layerCount;              // 0x1F  //CNT -> subMaterials
    uint16_t packedAtlasDataSize;    // 0x20
    uint8_t textureAtlasRowCount;    // 0x22
    uint8_t textureAtlasColumnCount; // 0x23
    // 0x24..0x27 pad (16-align for drawSurf)
    uint8_t drawSurf[16];               // 0x28  GfxDrawSurf (16B packed bitfield; raw)
    uint8_t* packedAtlasData;           // 0x38  //PTR
    MaterialTechniqueSet* techniqueSet; // 0x40  //PTR
    MaterialTextureDef* textureTable;   // 0x48  //PTR  [textureCount]
    MaterialConstantDef* constantTable; // 0x50  //PTR  [constantCount] (stride 20)
    void* decalVolumeMaterial;          // 0x58  //PTR  GfxDecalVolumeMaterial*
    uint8_t* constantBufferIndex;       // 0x60  //PTR
    void* constantBufferTable; // 0x68  //PTR  MaterialConstantBufferDef* [constantBufferCount]
    const char** subMaterials; // 0x70  //PTR  [layerCount] (array of XString)
};
static_assert(sizeof(Material) == 0x78, "Material size (pinned 0x78)");
static_assert(sizeof(Material) == iw8sz::MATERIAL, "Material size must equal g_assetSizes[11]");

// =============================================================================================
//  XModelLodInfo  [PDB] 0x40 — one per LOD (XModel.lodInfo[6])
// =============================================================================================
struct XModelLodInfo {
    XModelSurfs* modelSurfsStaging; // 0x00  //PTR
    XSurface* surfs;                // 0x08  //PTR  -> this LOD's XSurface[numsurfs]
    float dist;                     // 0x10
    uint16_t numsurfs;              // 0x14  //CNT
    uint16_t surfIndex;             // 0x16
    uint8_t partBits[32];           // 0x18  DObjPartBits (0x20) — scalar
    int32_t subdivLodValidMask;     // 0x38  (volatile)
    uint8_t flags;                  // 0x3C  XModelLodInfoFlags
    // 0x3D..0x3F pad
};
static_assert(sizeof(XModelLodInfo) == 0x40, "XModelLodInfo");

// =============================================================================================
//  XModel  (asset type 9)
//  [PDB] dev sizeof = 0x2A8.   [TBL] retail g_assetSizes[9] = 0x2B0.  >>> 8-BYTE DELTA <<<
//  The dev (IW8_DEV 8.24 Xbox) build is 8 bytes SHORTER than retail PC 1.24. The dev field
//  map below is exact for 0x00..0x2A8 (last field decalVolumesInfo ends at 0x2A8). Retail
//  appends 8 more bytes at 0x2A8 (one extra trailing pointer/qword — NOT pinned this pass,
//  marked [INFER]). To satisfy the g_assetSizes invariant the writer struct adds an explicit
//  8-byte tail. When the writer emits a *valid-empty / minimal* XModel it zeroes the tail,
//  which is load-safe (no extra count/pointer to walk). RESOLVE before shipping real models.
// =============================================================================================
struct XModel {
    const char* name;                // 0x00  //PTR
    uint16_t numsurfs;               // 0x08
    uint8_t numLods;                 // 0x0A
    uint8_t collLod;                 // 0x0B
    uint16_t mdaoVolumeCount;        // 0x0C  //CNT -> mdaoVolumes
    uint8_t shadowCutoffLod;         // 0x0E
    uint8_t physicsUseCategory;      // 0x0F
    uint8_t characterCollBoundsType; // 0x10
    uint8_t numAimAssistBones;       // 0x11  //CNT -> aimAssistBones
    uint8_t impactType;              // 0x12
    uint8_t mdaoType;                // 0x13
    uint8_t numBones;                // 0x14  //CNT -> boneNames/parentList/...
    uint8_t numRootBones;            // 0x15
    uint16_t numClientBones;         // 0x16
    uint8_t numClothAssets;          // 0x18  //CNT -> clothAssets
    // 0x19..0x1B pad
    uint32_t flags;                  // 0x1C
    int32_t contents;                // 0x20
    float scale;                     // 0x24
    float radius;                    // 0x28
    Bounds bounds;                   // 0x2C  (0x18)
    float edgeLength;                // 0x44
    uint32_t lgvData;                // 0x48
    uint8_t physicsUsageCounter[12]; // 0x4C  XModelPhysicsUsageCounter (3*int) — scalar
    uint32_t noScalePartBits[8];     // 0x58  (0x20) — scalar
    void* scriptableMoverDef;        // 0x78  //PTR  ScriptableDef*
    void* proceduralBones;           // 0x80  //PTR  XAnimProceduralBones*
    void* dynamicBones;              // 0x88  //PTR  XAnimDynamicBones*
    scr_string_t* aimAssistBones;    // 0x90  //PTR  [numAimAssistBones]
    scr_string_t* boneNames;         // 0x98  //PTR  [numBones]
    uint8_t* parentList;             // 0xA0  //PTR  [numBones-numRootBones]
    int16_t* quats;                  // 0xA8  //PTR
    float* trans;                    // 0xB0  //PTR
    uint8_t* partClassification;     // 0xB8  //PTR
    void* baseMat;                   // 0xC0  //PTR  DObjAnimMat* [numBones]
    vec3_t* ikHingeAxis;             // 0xC8  //PTR
    void* reactiveMotionInfo;        // 0xD0  //PTR  ReactiveMotionModelInfo*
    Material** materialHandles;      // 0xD8  //PTR  [numsurfs] -> Material*
    XModelLodInfo lodInfo[6];        // 0xE0  (0x180) — inline; each has 2 //PTR
    void* boneInfo;                  // 0x260 //PTR  XBoneInfo* [numBones]
    float* himipRadiusInvSq;         // 0x268 //PTR
    void* physicsAsset;              // 0x270 //PTR  PhysicsAsset*
    void* physicsFXShape;            // 0x278 //PTR  PhysicsFXShape*
    void* detailCollision;           // 0x280 //PTR  XModelDetailCollision*
    void** clothAssets;              // 0x288 //PTR  [numClothAssets] -> ClothAsset*
    void* blendShapeInfo;            // 0x290 //PTR  XModelBlendShapeInfo*
    void* mdaoVolumes;               // 0x298 //PTR  MdaoVolume* [mdaoVolumeCount]
    void* decalVolumesInfo;          // 0x2A0 //PTR  XModelDecalVolumesInfo*  (dev ends @0x2A8)
    uint8_t _retailTail[8]; // 0x2A8 //[INFER] retail-only 8B (g_assetSizes=0x2B0). zero=safe.
};
static_assert(sizeof(XModel) == 0x2B0, "XModel size (pinned retail 0x2B0)");
static_assert(sizeof(XModel) == iw8sz::XMODEL,
              "XModel size must equal retail g_assetSizes[9]=0x2B0");

struct XModelSurfs {
    const char* name;        // 0x00  //PTR  XString
    XSurface* surfs;         // 0x08  //PTR  [numsurfs]
    XPakEntryInfo xpakEntry; // 0x10  (0x20)  — scalar streamed-geo locator
    XSurfaceShared* shared;  // 0x30  //PTR  -> shared vert/index/tri blob
    uint16_t numsurfs;       // 0x38  //CNT
    uint8_t ugbState;        // 0x3A  XModelSurfsUGBState
    // 0x3B pad
    uint8_t partBits[32]; // 0x3C  DObjPartBits (0x20) — scalar (ends 0x5C)
    // 0x5C..0x5F pad
};
static_assert(sizeof(XModelSurfs) == 0x60, "XModelSurfs size (pinned 0x60)");
static_assert(sizeof(XModelSurfs) == iw8sz::XMODELSURFS,
              "XModelSurfs size must equal g_assetSizes[8]");

// =============================================================================================
//  XSurfaceShared  [PDB] 0x10 — the shared geometry blob descriptor (verts/indices/tris live
//  HERE as one packed buffer; XSurface references regions of it via the *Offset fields below).
//  XSurfaceSharedData data @0 is an 8-byte union holding the pointer to the packed blob. //PTR.
// =============================================================================================
struct XSurfaceShared {
    void* data;        // 0x00  //PTR  XSurfaceSharedData (packed geometry bytes, dataSize long)
    uint32_t dataSize; // 0x08  //CNT (bytes in .data)
    uint32_t flags;    // 0x0C  XSurfaceSharedFlags
};
static_assert(sizeof(XSurfaceShared) == 0x10, "XSurfaceShared");

// =============================================================================================
//  XRigidVertList  [PDB] 0x08 — rigid (single-bone) vert run (XSurface.rigidVertLists[])
// =============================================================================================
struct XRigidVertList {
    uint16_t boneIndexOffset; // 0x00
    uint16_t vertCount;       // 0x02
    uint16_t triOffset;       // 0x04
    uint16_t triCount;        // 0x06
};
static_assert(sizeof(XRigidVertList) == 0x08, "XRigidVertList");

// =============================================================================================
//  XSurface  (per-LOD surface; pointed to by XModelLodInfo.surfs / XModelSurfs.surfs)
//  [PDB] 0xC0. NOT a standalone asset (no g_assetSizes entry).
//  In IW8 the bulk vertex/index/weight DATA is NOT inline in XSurface — it lives in the shared
//  buffer (XSurfaceShared.data) and XSurface stores only the *Offset/*Count fields + a few
//  per-surface pointers. The "vertex/index/weight sub-structs" are: GfxPackedVertex (0x14,
//  the packed render vertex inside .data), packed u16 triangle indices (inside .data via
//  sharedIndexDataOffset), and XRigidVertList (0x08, the weight/rigid-run table referenced by
//  rigidVertLists). lmapCoords -> PackedLmapCoords (0x04 each).
//  Pointer fields: shared, lmapCoords, rigidVertLists, blendVerts, subdiv, childBounds,
//                  blendShapesPerVert, blendShapesRecalcTangentFrameData.
// =============================================================================================
struct XSurface {
    uint16_t flags;                      // 0x00
    uint16_t vertCount;                  // 0x02  //CNT (render verts in shared blob)
    uint16_t triCount;                   // 0x04  //CNT (tris/indices in shared blob)
    uint16_t blendShapeTargetCount;      // 0x06
    uint8_t rigidVertListCount;          // 0x08  //CNT -> rigidVertLists
    uint8_t subdivLevelCount;            // 0x09
    uint16_t ugbID;                      // 0x0A
    uint32_t hash;                       // 0x0C
    uint16_t blendVertCounts[8];         // 0x10  (0x10) — scalar
    uint32_t blendVertSize;              // 0x20
    uint32_t sharedVertDataOffset;       // 0x24  (offset into XSurfaceShared.data — render verts)
    uint32_t sharedIndexDataOffset;      // 0x28  (offset — u16 triangle indices)
    uint32_t sharedTriClusterDataOffset; // 0x2C
    uint32_t sharedColorDataOffset;      // 0x30
    uint32_t sharedSecondUVDataOffset;   // 0x34
    uint32_t sharedNormalTransformDataOffset; // 0x38
    uint32_t sharedTensionAccumTableOffset;   // 0x3C
    uint32_t sharedTensionDataOffset;         // 0x40
    // 0x44..0x47 pad
    XSurfaceShared* shared;         // 0x48  //PTR  -> the packed geometry blob
    void* lmapCoords;               // 0x50  //PTR  PackedLmapCoords* (0x04 each) [vertCount]
    XRigidVertList* rigidVertLists; // 0x58  //PTR  [rigidVertListCount]
    uint16_t* blendVerts;           // 0x60  //PTR
    void* subdiv;                   // 0x68  //PTR  XSurfaceSubdivInfo* (0x60)
    uint8_t partBits[32];           // 0x70  DObjPartBits (0x20) — scalar
    Bounds surfBounds;              // 0x90  (0x18)
    Bounds* childBounds;            // 0xA8  //PTR
    void* blendShapesPerVert;       // 0xB0 //PTR BlendShapesPerVert*(0x20)
    void* blendShapesRecalcTangentFrameData; // 0xB8 //PTR
};
static_assert(sizeof(XSurface) == 0xC0, "XSurface");

// =============================================================================================
//  GfxPackedVertex  [PDB] 0x14 — the packed render vertex stored inside XSurfaceShared.data.
//  Documented for the geometry writer (it is NOT a member of XSurface; it is blob content).
//  DISCREPANCY: the on-disk stride is exactly 0x14 (20 bytes, tightly packed in .data), but under
//  C++ NATURAL alignment the uint64_t xyz forces 8-byte struct alignment → sizeof would round up
//  to 0x18. There is no g_assetSizes entry for this blob element, so to stay BYTE-FAITHFUL to the
//  real 20-byte stride this single struct is pinned to pack(1). It is never embedded by value in
//  any asset struct above (it is referenced only as raw blob content via *Offset), so the local
//  pack does not perturb any other layout.
// =============================================================================================
#pragma pack(push, 1)
struct GfxPackedVertex {
    uint64_t xyz;            // 0x00  PackedPosition
    uint32_t selfVisibility; // 0x08  PackedSelfVisibility
    uint32_t texCoord;       // 0x0C  PackedTexCoords
    uint32_t tangentFrame;   // 0x10  PackedQuatDec3n
};
static_assert(sizeof(GfxPackedVertex) == 0x14, "GfxPackedVertex disk stride must be 20 (0x14)");
#pragma pack(pop)

} // namespace iw8_focus
