#pragma once
#include <cstdint>

// Pull in the iw8sz:: g_assetSizes constants (XMODELSURFS/XMODEL/MATERIAL/IMAGE) for the dual
// size-invariant static_asserts. iw8_structs.h restores default packing (pop) before its include
// guard ends, so the natural-alignment structs below are unaffected by its pack(1) region.
#include "iw8/iw8_structs.h"

namespace iw8_focus
{
// ---- shared scalar aliases (match the dev PDB types) ----------------------------------------
using scr_string_t = uint32_t; // [PDB] script-string handle (u32)
struct vec2_t
{
    float v[2];
};
struct vec3_t
{
    float v[3];
};
struct vec4_t
{
    float v[4];
}; // [PDB] size 0x10
struct Bounds
{
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
struct XPakEntryInfo
{
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
struct GfxImageStreamData
{
    XPakEntryInfo xpakEntry;    // 0x00  (0x20)  — scalar
    uint32_t levelCountAndSize; // 0x20  GfxImageStreamLevelCountAndSize (packed u32)
    uint16_t width;             // 0x24
    uint16_t height;            // 0x26
};
static_assert(sizeof(GfxImageStreamData) == 0x28, "GfxImageStreamData");

// =============================================================================================
//  GfxImageFallback  [PDB] 0x10 — low-res inline fallback pixels (pointed to by image.fallback)
// =============================================================================================
struct GfxImageFallback
{
    uint8_t *pixels; // 0x00  //PTR  -> raw pixel bytes (size = .size)
    uint32_t size;   // 0x08  //CNT
    uint16_t width;  // 0x0C
    uint16_t height; // 0x0E
};
static_assert(sizeof(GfxImageFallback) == 0x10, "GfxImageFallback");

// =============================================================================================
//  GfxImage  (asset type 19)  [PDB] 0xE8  ==  [TBL] g_assetSizes[19]=0xE8   CONFIRMED
//  Pointer fields: name, packedAtlasData, fallback, pixels. (streams[] is inline, scalar.)
// =============================================================================================
struct GfxImage
{
    const char *name;            // 0x00  //PTR  XString
    uint8_t *packedAtlasData;    // 0x08  //PTR  (atlas blob; size = Material-side, often null)
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
    GfxImageFallback *fallback;    // 0xD8  //PTR
    // GfxImagePixels pixels @0xE0 — [PDB] size 8: a pointer-sized union (the resident pixel
    // pointer). //PTR-ish (runtime/streamed). For an inline-pixels converter, the actual DXT
    // bytes are emitted via the streams/raw path; write this slot 0 (or follows if inlining).
    void *pixels; // 0xE0  GfxImagePixels (8B) //PTR(runtime)
};
static_assert(sizeof(GfxImage) == 0xE8, "GfxImage size (pinned 0xE8)");
static_assert(sizeof(GfxImage) == iw8sz::IMAGE, "GfxImage size must equal g_assetSizes[19]");

// =============================================================================================
//  MaterialTextureDef  [PDB] 0x10 — one per bound image (Material.textureTable[textureCount])
//  NOTE: IW8 dev layout is the SIMPLE form {index, image}, NOT the H1/MWR nameHash form.
// =============================================================================================
struct MaterialTextureDef
{
    uint8_t index; // 0x00  (sampler/dest slot)
    // 0x01..0x07 pad
    GfxImage *image; // 0x08  //PTR  -> bound GfxImage
};
static_assert(sizeof(MaterialTextureDef) == 0x10, "MaterialTextureDef");

// =============================================================================================
//  MaterialConstantDef  [PDB] fields + [LOAD] stride. Load_MaterialConstantDefArray does
//  Load_Stream(.., 20*count) => ARRAY STRIDE = 20 (0x14), tightly packed. (H1's 0x20 is WRONG
//  for IW8.)  All scalar; NO zone pointers.
//  DISCREPANCY: under C++ natural alignment the vec4_t literal at 0x04 makes the struct sizeof
//  0x14 anyway (4-byte align — no tail pad), so the disk stride 0x14 holds. Asserted below.
// =============================================================================================
struct MaterialConstantDef
{
    uint8_t index; // 0x00
    // 0x01..0x03 pad
    vec4_t literal; // 0x04  (16 bytes)
}; // total 0x14 = 20  [LOAD-confirmed stride]
static_assert(sizeof(MaterialConstantDef) == 0x14, "MaterialConstantDef stride must be 20 (0x14)");

// =============================================================================================
//  GfxStateBits  [PDB] 0x10 — two 64-bit packed render-state words. All scalar.
// =============================================================================================
struct GfxStateBits
{
    uint64_t otherBits; // 0x00  GfxOtherStateBits
    uint64_t blendBits; // 0x08  GfxBlendStateBits
};
static_assert(sizeof(GfxStateBits) == 0x10, "GfxStateBits");

// =============================================================================================
//  Material  (asset type 11)  [PDB] 0x78  ==  [TBL] g_assetSizes[11]=0x78   CONFIRMED
//  Pointer fields: name, packedAtlasData, techniqueSet, textureTable, constantTable,
//                  decalVolumeMaterial, constantBufferIndex, constantBufferTable, subMaterials.
//  Count fields:   textureCount->textureTable, constantCount->constantTable,
//                  constantBufferCount->constantBufferTable, layerCount->subMaterials.
//  GfxStateBits is NOT inlined in Material in IW8 (it lives off the techniqueSet/technique).
// =============================================================================================
struct Material
{
    const char *name;                // 0x00  //PTR  XString
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
    uint8_t *packedAtlasData;           // 0x38  //PTR
    MaterialTechniqueSet *techniqueSet; // 0x40  //PTR
    MaterialTextureDef *textureTable;   // 0x48  //PTR  [textureCount]
    MaterialConstantDef *constantTable; // 0x50  //PTR  [constantCount] (stride 20)
    void *decalVolumeMaterial;          // 0x58  //PTR  GfxDecalVolumeMaterial*
    uint8_t *constantBufferIndex;       // 0x60  //PTR
    void *constantBufferTable; // 0x68  //PTR  MaterialConstantBufferDef* [constantBufferCount]
    const char **subMaterials; // 0x70  //PTR  [layerCount] (array of XString)
};
static_assert(sizeof(Material) == 0x78, "Material size (pinned 0x78)");
static_assert(sizeof(Material) == iw8sz::MATERIAL, "Material size must equal g_assetSizes[11]");

// =============================================================================================
//  XModelLodInfo  [PDB] 0x40 — one per LOD (XModel.lodInfo[6])
// =============================================================================================
struct XModelLodInfo
{
    XModelSurfs *modelSurfsStaging; // 0x00  //PTR
    XSurface *surfs;                // 0x08  //PTR  -> this LOD's XSurface[numsurfs]
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
//  Replay 1.20 reads a 0x2B0-byte body. Its final four qwords are blendShapeInfo at 0x290,
//  an opaque runtime-only qword at 0x298, mdaoVolumes at 0x2A0, and decalVolumesInfo at 0x2A8.
//  The runtime qword is copied with the body but is not traversed by the native asset loader.
// =============================================================================================
struct XModel
{
    const char *name;                // 0x00  //PTR
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
    void *scriptableMoverDef;        // 0x78  //PTR  ScriptableDef*
    void *proceduralBones;           // 0x80  //PTR  XAnimProceduralBones*
    void *dynamicBones;              // 0x88  //PTR  XAnimDynamicBones*
    scr_string_t *aimAssistBones;    // 0x90  //PTR  [numAimAssistBones]
    scr_string_t *boneNames;         // 0x98  //PTR  [numBones]
    uint8_t *parentList;             // 0xA0  //PTR  [numBones-numRootBones]
    int16_t *quats;                  // 0xA8  //PTR
    float *trans;                    // 0xB0  //PTR
    uint8_t *partClassification;     // 0xB8  //PTR
    void *baseMat;                   // 0xC0  //PTR  DObjAnimMat* [numBones]
    vec3_t *ikHingeAxis;             // 0xC8  //PTR
    void *reactiveMotionInfo;        // 0xD0  //PTR  ReactiveMotionModelInfo*
    Material **materialHandles;      // 0xD8  //PTR  [numsurfs] -> Material*
    XModelLodInfo lodInfo[6];        // 0xE0  (0x180) — inline; each has 2 //PTR
    void *boneInfo;                  // 0x260 //PTR  XBoneInfo* [numBones]
    float *himipRadiusInvSq;         // 0x268 //PTR
    void *physicsAsset;              // 0x270 //PTR  PhysicsAsset*
    void *physicsFXShape;            // 0x278 //PTR  PhysicsFXShape*
    void *detailCollision;           // 0x280 //PTR  XModelDetailCollision*
    void **clothAssets;              // 0x288 //PTR  [numClothAssets] -> ClothAsset*
    void *blendShapeInfo;            // 0x290 //PTR  XModelBlendShapeInfo*
    uint64_t runtime_0x298;          // 0x298 runtime-only; not traversed by Load_XModel
    void *mdaoVolumes;               // 0x2A0 //PTR  MdaoVolume* [mdaoVolumeCount]
    void *decalVolumesInfo;          // 0x2A8 //PTR  XModelDecalVolumesInfo*
};
static_assert(offsetof(XModel, blendShapeInfo) == 0x290, "XModel.blendShapeInfo");
static_assert(offsetof(XModel, runtime_0x298) == 0x298, "XModel.runtime_0x298");
static_assert(offsetof(XModel, mdaoVolumes) == 0x2A0, "XModel.mdaoVolumes");
static_assert(offsetof(XModel, decalVolumesInfo) == 0x2A8, "XModel.decalVolumesInfo");
static_assert(sizeof(XModel) == 0x2B0, "XModel size (pinned retail 0x2B0)");
static_assert(sizeof(XModel) == iw8sz::XMODEL,
              "XModel size must equal retail g_assetSizes[9]=0x2B0");

// =============================================================================================
//  XModelSurfs  (asset type 8)  [PDB] 0x60  ==  [TBL] g_assetSizes[8]=0x60   CONFIRMED
//  Pointer fields: name, surfs, shared.  Count: numsurfs -> surfs[].
// =============================================================================================
struct XModelSurfs
{
    const char *name;        // 0x00  //PTR  XString
    XSurface *surfs;         // 0x08  //PTR  [numsurfs]
    XPakEntryInfo xpakEntry; // 0x10  (0x20)  — scalar streamed-geo locator
    XSurfaceShared *shared;  // 0x30  //PTR  -> shared vert/index/tri blob
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
struct XSurfaceShared
{
    void *data;        // 0x00  //PTR  XSurfaceSharedData (packed geometry bytes, dataSize long)
    uint32_t dataSize; // 0x08  //CNT (bytes in .data)
    uint32_t flags;    // 0x0C  XSurfaceSharedFlags
};
static_assert(sizeof(XSurfaceShared) == 0x10, "XSurfaceShared");

// =============================================================================================
//  XRigidVertList  [PDB] 0x08 — rigid (single-bone) vert run (XSurface.rigidVertLists[])
// =============================================================================================
struct XRigidVertList
{
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
struct XSurface
{
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
    XSurfaceShared *shared;         // 0x48  //PTR  -> the packed geometry blob
    void *lmapCoords;               // 0x50  //PTR  PackedLmapCoords* (0x04 each) [vertCount]
    XRigidVertList *rigidVertLists; // 0x58  //PTR  [rigidVertListCount]
    uint16_t *blendVerts;           // 0x60  //PTR
    void *subdiv;                   // 0x68  //PTR  XSurfaceSubdivInfo* (0x60)
    uint8_t partBits[32];           // 0x70  DObjPartBits (0x20) — scalar
    Bounds surfBounds;              // 0x90  (0x18)
    Bounds *childBounds;            // 0xA8  //PTR
    void *blendShapesPerVert;       // 0xB0 //PTR BlendShapesPerVert*(0x20)
    void *blendShapesRecalcTangentFrameData; // 0xB8 //PTR
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
struct GfxPackedVertex
{
    uint64_t xyz;            // 0x00  PackedPosition
    uint32_t selfVisibility; // 0x08  PackedSelfVisibility
    uint32_t texCoord;       // 0x0C  PackedTexCoords
    uint32_t tangentFrame;   // 0x10  PackedQuatDec3n
};
static_assert(sizeof(GfxPackedVertex) == 0x14, "GfxPackedVertex disk stride must be 20 (0x14)");
#pragma pack(pop)

// Replay 1.20 ParticleSystemDef (asset 44). Offsets are pinned against the MW19
// loader schema and the shipped common.ff glass VFX capture. These describe the
// in-memory ABI. The direct Replay loader trace for that fixture loads this root
// in block 1, then the name and emitter/state/group/module records in block 5;
// selector-specific module union writes still require a complete writer contract.
struct ParticleFloatRange
{
    float min;
    float max;
};
struct ParticleIntRange
{
    int32_t min;
    int32_t max;
};
struct ParticleCurveControlPointDef
{
    float time;
    float value;
    float invTimeDelta;
    uint32_t pad;
};
struct ParticleCurveDef
{
    ParticleCurveControlPointDef *controlPoints;
    int32_t numControlPoints;
    float scale;
};
static_assert(sizeof(ParticleFloatRange) == 0x8);
static_assert(sizeof(ParticleIntRange) == 0x8);
static_assert(sizeof(ParticleCurveControlPointDef) == 0x10);
static_assert(sizeof(ParticleCurveDef) == 0x10);

// Replay 1.20 selectors needed by the IW3 impacts/small_glass graph. Do not
// substitute the older game-test/x64-zt enum: its INIT_MODEL is 12, while the
// shipped Replay schema and captured assets use 13 after INIT_KILL_WRAP_BOX.
enum class ParticleModuleType : uint16_t
{
    initAtlas = 0,
    initAttributes = 1,
    initCloud = 4,
    initDecal = 5,
    initLightOmni = 9,
    initMaterial = 11,
    initMirrorTexture = 12,
    initModel = 13,
    initOrientedSprite = 15,
    initRelativeVelocity = 18,
    initRotation = 19,
    initRotation3D = 20,
    initRunner = 21,
    initSpawn = 23,
    initSpawnShapeBox = 24,
    initSpawnShapeCylinder = 25,
    initSpawnShapeSphere = 28,
    initTail = 29,
    colorGraph = 34,
    forceDragGraph = 40,
    gravity = 41,
    physicsRayCast = 46,
    sizeGraph = 51,
    velocityGraph = 53,
    testBirth = 56,
    testDeath = 57,
    testImpact = 59,
};

// INIT_MODEL (selector 13) has a 0x10 linked-asset list at +0x10 of its
// 0x20 module variant. Shipped snowball VFX loads three 0x20 linked records,
// each pointing to a 0x2B0 native XModel reference root in block 1.
struct ParticleLinkedAssetDef
{
    void *asset; // XModel* for INIT_MODEL; other selectors reuse this union
    uint8_t selectorData[0x18];
};
struct ParticleLinkedAssetListDef
{
    ParticleLinkedAssetDef *assets;
    int32_t numAssets;
    uint32_t pad;
};
struct ParticleModuleInitModel
{
    uint64_t base;
    uint8_t usePhysics;
    uint8_t motionBlurHQ;
    uint8_t pad[6];
    ParticleLinkedAssetListDef linkedAssets;
};
static_assert(sizeof(ParticleLinkedAssetDef) == 0x20);
static_assert(sizeof(ParticleLinkedAssetListDef) == 0x10);
static_assert(sizeof(ParticleModuleInitModel) == 0x20);
static_assert(offsetof(ParticleModuleInitModel, linkedAssets) == 0x10);

// Replay schema: these selector payloads are embedded at +0x10 of a
// ParticleModuleDef. INIT_SPAWN owns a curve pointer; INIT_ATTRIBUTES carries
// the authored size/color/velocity ranges needed by source IW3 effects.
struct ParticleModuleBase
{
    uint16_t type;
    uint16_t pad;
    uint32_t flags;
};
struct ParticleModuleInitSpawn
{
    ParticleModuleBase base;
    uint32_t pad[2];
    ParticleCurveDef curve;
};
struct ParticleModuleInitAttributes
{
    ParticleModuleBase base;
    uint8_t interpolateColor;
    uint8_t interpolateSize;
    uint8_t pad[6];
    vec4_t sizeMin;
    vec4_t sizeMax;
    vec4_t colorMin;
    vec4_t colorMax;
    vec4_t velocityMin;
    vec4_t velocityMax;
};
static_assert(sizeof(ParticleModuleBase) == 0x8);
static_assert(sizeof(ParticleModuleInitSpawn) == 0x20);
static_assert(offsetof(ParticleModuleInitSpawn, curve) == 0x10);
static_assert(sizeof(ParticleModuleInitAttributes) == 0x70);
static_assert(offsetof(ParticleModuleInitAttributes, sizeMin) == 0x10);
static_assert(offsetof(ParticleModuleInitAttributes, velocityMax) == 0x60);

struct ParticleModuleInitMaterial
{
    ParticleModuleBase base;
    uint32_t renderOptions;
    uint32_t shaderGraphOptions;
    ParticleLinkedAssetListDef linkedAssets;
    uint8_t materialData[0xC0];
};
struct ParticleModuleInitLightOmni
{
    ParticleModuleBase base;
    ParticleLinkedAssetListDef linkedAssets;
    float fovOuter;
    float fovInner;
    float bulbRadius;
    float bulbLength;
    float distanceFalloff;
    float brightness;
    float intensityUV;
    float intensityIR;
    float intensityHeat;
    float shadowSoftness;
    float shadowBias;
    float shadowArea;
    float toneMappingScaleFactor;
    uint8_t disableVolumetric;
    uint8_t disableShadowMap;
    uint8_t disableDynamicShadows;
    uint8_t scriptScale;
};
struct ParticleModuleInitDecal
{
    ParticleModuleBase base;
    uint16_t fadeInTime;
    uint16_t fadeOutTime;
    uint16_t stoppableFadeOutTime;
    uint16_t lerpWaitTime;
    ParticleLinkedAssetListDef linkedAssets;
    vec4_t lerpColor;
    uint16_t lerpTime;
    uint8_t dynamicDecal;
    uint8_t projectionAxis;
    uint8_t bypassStackingLimiter;
    uint8_t pad[11];
};
// Replay schema selector 21: two value-only ParticleModifier records precede
// the linked child ParticleSystemDef list at +0x50.
struct ParticleModuleInitRunner
{
    ParticleModuleBase base;
    uint32_t pad[2];
    vec4_t scaleMin;
    vec4_t scaleMax;
    vec4_t velocityMin;
    vec4_t velocityMax;
    ParticleLinkedAssetListDef linkedAssets;
    uint8_t orientationOptions;
    uint8_t scaleOptions;
    uint8_t velocityOptions;
    uint8_t attachToParent;
    uint8_t stopChildOnDeath;
    uint8_t killChildOnDeath;
    uint8_t legacyOrientationVelocity;
    uint8_t legacyOrientationRotation;
    uint8_t padOptions[8];
};
struct ParticleModuleInitAtlas
{
    ParticleModuleBase base;
    int32_t startFrame;
    int32_t loopCount;
    uint8_t randomIndex;
    uint8_t playOverLife;
    uint8_t pad0[2];
    uint32_t pad1[3];
    ParticleCurveDef curves[2];
};
struct ParticleModuleColorGraph
{
    ParticleModuleBase base;
    uint8_t firstCurve;
    uint8_t pad0[3];
    uint8_t modulateColorByAlpha;
    uint8_t pad1[3];
    ParticleCurveDef curves[8];
};
struct ParticleModuleSizeGraph
{
    ParticleModuleBase base;
    uint8_t firstCurve;
    uint8_t pad[7];
    ParticleCurveDef curves[6];
    vec4_t sizeBegin;
    vec4_t sizeEnd;
};
struct ParticleModuleInitCloud
{
    ParticleModuleBase base;
    uint32_t pad[2];
};
struct ParticleModuleInitTail
{
    ParticleModuleBase base;
    uint16_t averagePastVelocities;
    uint16_t maxParentSpeed;
    uint8_t tailLeading;
    uint8_t scaleWithVelocity;
    uint8_t rotateAroundPivot;
    uint8_t pad;
};
struct ParticleModuleInitOrientedSprite
{
    ParticleModuleBase base;
    uint32_t pad[2];
    vec4_t orientationQuat;
};
struct ParticleModuleInitRelativeVelocity
{
    ParticleModuleBase base;
    uint32_t velocityType;
    uint8_t useBoltInfo;
    uint8_t pad[3];
};
struct ParticleModuleInitRotation
{
    ParticleModuleBase base;
    uint32_t pad[2];
    ParticleFloatRange rotationAngle;
    ParticleFloatRange rotationRate;
};
struct ParticleModuleInitRotation3D
{
    ParticleModuleBase base;
    uint32_t pad[2];
    vec4_t rotationAngleMin;
    vec4_t rotationAngleMax;
    vec4_t rotationRateMin;
    vec4_t rotationRateMax;
};
struct ParticleModuleGravity
{
    ParticleModuleBase base;
    ParticleFloatRange percentage;
};
struct ParticleBounds
{
    vec3_t midPoint;
    vec3_t halfSize;
};
struct ParticleModulePhysicsRayCast
{
    ParticleModuleBase base;
    ParticleFloatRange bounce;
    ParticleBounds bounds;
    uint8_t useItemClip;
    uint8_t useSurfaceType;
    uint8_t collideWithWater;
    uint8_t ignoreContentItem;
    uint8_t pad[4];
};
struct ParticleModifier
{
    vec4_t min;
    vec4_t max;
};
struct ParticleModuleTestEventHandlerData
{
    uint32_t nextState;
    uint32_t pad0;
    ParticleLinkedAssetListDef linkedAssets;
    uint8_t kill;
    uint8_t pad1[3];
    uint32_t pad2;
};
struct ParticleModuleTest
{
    ParticleModuleBase base;
    uint16_t moduleIndex;
    uint8_t orientationOptions;
    uint8_t scaleOptions;
    uint8_t velocityOptions;
    uint8_t pad[3];
    ParticleModifier scaleModifier;
    ParticleModifier velocityModifier;
    ParticleModuleTestEventHandlerData eventHandlerData;
};
struct ParticleModuleTestImpact
{
    ParticleModuleTest test;
    uint32_t impactDirection;
    uint8_t pad[12];
};
static_assert(sizeof(ParticleModuleInitMaterial) == 0xE0);
static_assert(offsetof(ParticleModuleInitMaterial, linkedAssets) == 0x10);
static_assert(sizeof(ParticleModuleInitLightOmni) == 0x50);
static_assert(offsetof(ParticleModuleInitLightOmni, linkedAssets) == 0x08);
static_assert(offsetof(ParticleModuleInitLightOmni, fovOuter) == 0x18);
static_assert(offsetof(ParticleModuleInitLightOmni, toneMappingScaleFactor) == 0x48);
static_assert(sizeof(ParticleModuleInitDecal) == 0x40);
static_assert(offsetof(ParticleModuleInitDecal, linkedAssets) == 0x10);
static_assert(sizeof(ParticleModuleInitRunner) == 0x70);
static_assert(offsetof(ParticleModuleInitRunner, linkedAssets) == 0x50);
static_assert(offsetof(ParticleModuleInitRunner, orientationOptions) == 0x60);
static_assert(offsetof(ParticleModuleInitRunner, killChildOnDeath) == 0x65);
static_assert(sizeof(ParticleModuleInitAtlas) == 0x40);
static_assert(offsetof(ParticleModuleInitAtlas, curves) == 0x20);
static_assert(sizeof(ParticleModuleColorGraph) == 0x90);
static_assert(offsetof(ParticleModuleColorGraph, curves) == 0x10);
static_assert(sizeof(ParticleModuleSizeGraph) == 0x90);
static_assert(offsetof(ParticleModuleSizeGraph, sizeBegin) == 0x70);
static_assert(sizeof(ParticleModuleInitCloud) == 0x10);
static_assert(sizeof(ParticleModuleInitTail) == 0x10);
static_assert(sizeof(ParticleModuleInitOrientedSprite) == 0x20);
static_assert(sizeof(ParticleModuleInitRelativeVelocity) == 0x10);
static_assert(offsetof(ParticleModuleInitRelativeVelocity, velocityType) == 0x08);
static_assert(sizeof(ParticleModuleInitRotation) == 0x20);
static_assert(offsetof(ParticleModuleInitRotation, rotationAngle) == 0x10);
static_assert(sizeof(ParticleModuleInitRotation3D) == 0x50);
static_assert(offsetof(ParticleModuleInitRotation3D, rotationAngleMin) == 0x10);
static_assert(sizeof(ParticleModuleGravity) == 0x10);
static_assert(sizeof(ParticleBounds) == 0x18);
static_assert(sizeof(ParticleModulePhysicsRayCast) == 0x30);
static_assert(offsetof(ParticleModulePhysicsRayCast, bounce) == 0x08);
static_assert(offsetof(ParticleModulePhysicsRayCast, bounds) == 0x10);
static_assert(sizeof(ParticleModifier) == 0x20);
static_assert(sizeof(ParticleModuleTestEventHandlerData) == 0x20);
static_assert(offsetof(ParticleModuleTestEventHandlerData, linkedAssets) == 0x08);
static_assert(sizeof(ParticleModuleTest) == 0x70);
static_assert(offsetof(ParticleModuleTest, eventHandlerData) == 0x50);
static_assert(sizeof(ParticleModuleTestImpact) == 0x80);
static_assert(offsetof(ParticleModuleTestImpact, impactDirection) == 0x70);

struct ParticleModuleInitSpawnShape
{
    ParticleModuleBase base;
    uint8_t axisFlags;
    uint8_t spawnFlags;
    uint8_t normalAxis;
    uint8_t spawnType;
    float volumeCubeRoot;
    vec4_t calculationOffset;
    vec4_t offset;
};
struct ParticleModuleInitSpawnShapeBox
{
    ParticleModuleInitSpawnShape base;
    uint8_t useBeamInfo;
    uint8_t pad[15];
    vec4_t dimensionsMin;
    vec4_t dimensionsMax;
    ParticleCurveDef curves[6];
};
struct ParticleModuleInitSpawnShapeCylinder
{
    ParticleModuleInitSpawnShape base;
    uint8_t hasRotation;
    uint8_t rotateCalculatedOffset;
    uint8_t pad[2];
    float halfHeight;
    ParticleFloatRange radius;
    vec4_t directionQuat;
    ParticleCurveDef curves[5];
};
struct ParticleModuleInitSpawnShapeSphere
{
    ParticleModuleInitSpawnShape base;
    uint32_t pad[2];
    ParticleFloatRange radius;
    ParticleCurveDef curves[4];
};
struct ParticleModuleForceDragGraph
{
    ParticleModuleBase base;
    uint32_t pad[2];
    ParticleCurveDef curves[2];
};
struct ParticleModuleVelocityGraph
{
    ParticleModuleBase base;
    uint32_t pad[2];
    ParticleCurveDef curves[6];
    vec4_t velocityBegin;
    vec4_t velocityEnd;
};
static_assert(sizeof(ParticleModuleInitSpawnShape) == 0x30);
static_assert(sizeof(ParticleModuleInitSpawnShapeBox) == 0xC0);
static_assert(offsetof(ParticleModuleInitSpawnShapeBox, curves) == 0x60);
static_assert(sizeof(ParticleModuleInitSpawnShapeCylinder) == 0xA0);
static_assert(offsetof(ParticleModuleInitSpawnShapeCylinder, curves) == 0x50);
static_assert(sizeof(ParticleModuleInitSpawnShapeSphere) == 0x80);
static_assert(offsetof(ParticleModuleInitSpawnShapeSphere, curves) == 0x40);
static_assert(sizeof(ParticleModuleForceDragGraph) == 0x30);
static_assert(offsetof(ParticleModuleForceDragGraph, curves) == 0x10);
static_assert(sizeof(ParticleModuleVelocityGraph) == 0x90);
static_assert(offsetof(ParticleModuleVelocityGraph, curves) == 0x10);
static_assert(offsetof(ParticleModuleVelocityGraph, velocityBegin) == 0x70);

struct ParticleModuleDef
{
    uint16_t moduleType;      // 0x00
    uint16_t gap;             // 0x02
    uint32_t pad[2];          // 0x04
    uint32_t padToModuleData; // 0x0C
    uint8_t moduleData[0xE0]; // 0x10, selector-specific union
};
struct ParticleModuleGroupDef
{
    ParticleModuleDef *moduleDefs; // 0x00
    int32_t numModules;            // 0x08
    uint8_t disabled;              // 0x0C
    uint8_t pad[3];                // 0x0D
};
struct ParticleStateDef
{
    ParticleModuleGroupDef *moduleGroupDefs; // 0x00, exactly three groups
    uint32_t elementType;                    // 0x08
    uint32_t pad0;                           // 0x0C
    uint64_t flags;                          // 0x10
    uint32_t pad1[2];                        // 0x18
};
static_assert(sizeof(ParticleModuleDef) == 0xF0);
static_assert(offsetof(ParticleModuleDef, moduleData) == 0x10);
static_assert(sizeof(ParticleModuleGroupDef) == 0x10);
static_assert(sizeof(ParticleStateDef) == 0x20);

struct ParticleEmitterDef
{
    ParticleStateDef *stateDefs;                  // 0x00
    int32_t numStates;                            // 0x08
    ParticleFloatRange particleSpawnRate;         // 0x0C
    ParticleFloatRange particleLife;              // 0x14
    ParticleFloatRange particleDelay;             // 0x1C
    uint32_t particleCountMax;                    // 0x24
    ParticleIntRange particleBurstCount;          // 0x28
    ParticleFloatRange emitterLife;               // 0x30
    ParticleFloatRange emitterDelay;              // 0x38
    int32_t randomSeed;                           // 0x40
    ParticleFloatRange spawnRangeSq;              // 0x44
    float fadeOutMaxDistance;                     // 0x4C
    ParticleCurveDef fadeCurveDef;                // 0x50
    float spawnFrustumCullRadius;                 // 0x60
    uint32_t flags;                               // 0x64
    uint32_t gravityOptions;                      // 0x68
    uint32_t groupIDs[4];                         // 0x6C
    ParticleFloatRange emitByDistanceDensity;     // 0x7C
    uint32_t instancePool;                        // 0x84
    uint32_t soloInstanceMax;                     // 0x88
    uint32_t instanceAction;                      // 0x8C
    uint32_t dataFlags;                           // 0x90
    ParticleFloatRange particleSpawnShapeRange;   // 0x94
    uint32_t pad;                                 // 0x9C
};
static_assert(sizeof(ParticleEmitterDef) == 0xA0);
static_assert(offsetof(ParticleEmitterDef, fadeCurveDef) == 0x50);
static_assert(offsetof(ParticleEmitterDef, particleSpawnShapeRange) == 0x94);

struct ParticleSystemDef
{
    const char *name;                         // 0x00
    ParticleEmitterDef *emitterDefs;           // 0x08
    void *scriptedInputNodeDefs;              // 0x10
    int32_t version;                          // 0x18
    int32_t numEmitters;                      // 0x1C
    int32_t numScriptedInputNodes;            // 0x20
    uint32_t flags;                           // 0x24
    int32_t occlusionOverrideEmitterIndex;    // 0x28
    uint32_t phaseOptions;                    // 0x2C
    float drawFrustumCullRadius;              // 0x30
    float updateFrustumCullRadius;            // 0x34
    float sunDistance;                        // 0x38
    int32_t preRollMSec;                      // 0x3C
    vec4_t editorPosition;                    // 0x40
    vec4_t editorRotation;                    // 0x50
    vec4_t gameTweakPosition;                 // 0x60
    vec4_t gameTweakRotation;                 // 0x70
};
static_assert(sizeof(ParticleSystemDef) == iw8sz::VFX);
static_assert(offsetof(ParticleSystemDef, emitterDefs) == 0x08);
static_assert(offsetof(ParticleSystemDef, editorPosition) == 0x40);
static_assert(offsetof(ParticleSystemDef, gameTweakRotation) == 0x70);

} // namespace iw8_focus
