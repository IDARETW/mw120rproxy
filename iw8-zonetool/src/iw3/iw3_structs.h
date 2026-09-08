// iw3_structs.h — IW3 (CoD4) zone-asset structs, the 32-bit subset our offline loader needs.
// Ported verbatim from ..\zonetool\zonetool-develop\src\IW3\Structs.hpp (the authoritative layout):
// 32-bit pointers, #pragma pack(4). Anchors retained for cross-reference (Structs.hpp line numbers).
//
// SCOPE: only the FOCUS families (SPEC §1: material, image, xmodel, xsurface, maps). Pointer fields
// are kept as real typed 32-bit pointers (iw3_ptr<T>) so the offline loader can walk them; deep
// collision sub-graphs in clipMap_t that the prototype does NOT dump are typed iw3_ptr<void> with a
// // (unwalked) note. This header is data-only (no game code) and must compile 64-bit-host-clean.
#pragma once
#include <cstdint>

namespace iw3 {

#pragma pack(push, 4)

// A 32-bit in-zone pointer field. On a 64-bit host we store it as a u32 (the serialized width). The
// offline loader fixes these up to host buffer offsets via the IW3 tagged-pointer convention; the
// per-asset readers treat a resolved value as an index/offset into the inflated zone buffer.
template <typename T>
struct iw3_ptr {
    uint32_t off;   // raw serialized 32-bit pointer value (0=null, -1=follows, else packed offset)
};
static_assert(sizeof(iw3_ptr<int>) == 4, "iw3_ptr must serialize as 4 bytes");

// ---- common vector/bounds ------------------------------------------------------------------------
struct vec2_t { float v[2]; };
struct vec3_t { float v[3]; };
struct vec4_t { float v[4]; };

// ===================================================================================================
// IMAGE (Structs.hpp@110) — GfxImage + the inline GfxImageLoadDef payload.
// ===================================================================================================
struct GfxImageLoadDef {
    char    levelCount;
    char    flags;
    int16_t dimensions[3];
    int     format;
    int     resourceSize;
    char    data[1];   // variable-length inline pixel blob (DXT/RGBA mips), resourceSize bytes
};

struct GfxImage {
    int      mapType;          // MapType
    iw3_ptr<GfxImageLoadDef> texture;   // union GfxTexture { GfxImageLoadDef* loadDef; void* data; }
    char     picmip_a;         // Picmip { char platform[2]; }
    char     picmip_b;
    bool     noPicmip;
    char     semantic;
    char     track;
    uint32_t cardMemory;       // CardMemory { int platform[2]; } -> only first int needed; pad below
    uint32_t cardMemory_hi;
    uint16_t width;
    uint16_t height;
    uint16_t depth;
    char     category;
    bool     delayLoadPixels;
    iw3_ptr<char> name;
};

// On-disk IWi file header (Structs.hpp@142) — the dump container for image pixels.
struct GfxImageFileHeader {
    char  tag[3];
    char  version;
    char  format;
    char  flags;
    int16_t dimensions[3];
    int   fileSizeForPicmip[4];
};

// ===================================================================================================
// MATERIAL (Structs.hpp@431) + MaterialTextureDef (@212).
// ===================================================================================================
struct MaterialTextureDef {
    uint32_t typeHash;
    char     firstCharacter;
    char     secondLastCharacter;
    char     sampleState;
    char     semantic;
    iw3_ptr<GfxImage> image;   // union: image / water_t* (TS_WATER_MAP) — we only walk image
};

struct MaterialConstantDef {
    uint32_t nameHash;
    char     name[12];
    vec4_t   literal;
};

struct GfxStateBits {
    uint32_t loadBits[2];
};

struct Material {
    iw3_ptr<char> name;
    char     gameFlags;
    char     sortKey;
    char     textureAtlasRowCount;
    char     textureAtlasColumnCount;
    uint64_t drawSurf;                  // GfxDrawSurf (packed u64)
    uint32_t surfaceTypeBits;
    uint16_t hashIndex;
    unsigned char animationX;
    unsigned char animationY;
    char     stateBitsEntry[34];        // MaterialInfo-inlined
    char     numMaps;
    char     constantCount;
    char     stateBitsCount;
    char     stateFlags;
    char     cameraRegion;
    iw3_ptr<void> techniqueSet;         // MaterialTechniqueSet* (unwalked — techset deferred)
    iw3_ptr<MaterialTextureDef> maps;
    iw3_ptr<MaterialConstantDef> constantTable;
    iw3_ptr<GfxStateBits> stateMap;
};

// ===================================================================================================
// XSURFACE / XModelSurfs (Structs.hpp@482..567).
// ===================================================================================================
struct XSurfaceVertexInfo {
    int16_t  vertCount[4];
    iw3_ptr<uint16_t> vertsBlend;
};
struct GfxColor       { uint32_t packed; };
struct PackedTexCoords{ uint32_t packed; };
struct PackedUnitVec  { uint32_t packed; };
struct GfxPackedVertex {
    float          xyz[3];
    float          binormalSign;
    GfxColor       color;
    PackedTexCoords texCoord;
    PackedUnitVec  normal;
    PackedUnitVec  tangent;
};
struct XSurfaceCollisionAabb { uint16_t mins[3]; uint16_t maxs[3]; };
struct XSurfaceCollisionNode { XSurfaceCollisionAabb aabb; uint16_t childBeginIndex; uint16_t childCount; };
struct XSurfaceCollisionLeaf { uint16_t triangleBeginIndex; };
struct XSurfaceCollisionTree {
    float trans[3]; float scale[3];
    uint32_t nodeCount; iw3_ptr<XSurfaceCollisionNode> nodes;
    uint32_t leafCount; iw3_ptr<XSurfaceCollisionLeaf> leafs;
};
struct XRigidVertList {
    uint16_t boneOffset; uint16_t vertCount; uint16_t triOffset; uint16_t triCount;
    iw3_ptr<XSurfaceCollisionTree> collisionTree;
};
struct XSurface {
    char     tileMode;
    bool     deformed;
    uint16_t vertCount;
    uint16_t triCount;
    char     zoneHandle;
    uint16_t baseTriIndex;
    uint16_t baseVertIndex;
    iw3_ptr<uint16_t> triIndices;
    XSurfaceVertexInfo vertInfo;
    iw3_ptr<GfxPackedVertex> verts0;
    uint32_t vertListCount;
    iw3_ptr<XRigidVertList> vertList;
    int      partBits[4];
};

// ===================================================================================================
// XMODEL (Structs.hpp@672) + supporting (lod/bone/coll/phys).
// ===================================================================================================
struct DObjAnimMat { float quat[4]; float trans[3]; float transWeight; };
struct XBoneInfo   { float bounds[2][3]; float offset[3]; float radiusSquared; };
struct XModelLodInfo {
    float    dist; uint16_t numsurfs; uint16_t surfIndex; int partBits[4];
    char     lod; char smcIndexPlusOne; char smcAllocBits; char unused;
};
struct XModelHighMipBounds { float mins[3]; float maxs[3]; };
struct XModelStreamInfo { iw3_ptr<XModelHighMipBounds> highMipBounds; };
struct XModelCollTri_s { float plane[4]; float svec[4]; float tvec[4]; };
struct XModelCollSurf_s {
    iw3_ptr<XModelCollTri_s> collTris; int numCollTris;
    float mins[3]; float maxs[3]; int boneIdx; int contents; int surfFlags;
};
struct PhysPreset {
    iw3_ptr<char> name; int type; float mass; float bounce; float friction;
    float bulletForceScale; float explosiveForceScale; iw3_ptr<char> sndAliasPrefix;
    float piecesSpreadFraction; float piecesUpwardVelocity; char tempDefaultToCylinder;
};
struct PhysGeomList { uint32_t count; iw3_ptr<void> geoms; /*PhysGeomInfo* */ float mass[15]; };

struct XModel {
    iw3_ptr<char> name;
    char     numBones; char numRootBones; unsigned char numsurfs; char lodRampType;
    iw3_ptr<uint16_t> boneNames;
    iw3_ptr<char>     parentList;
    iw3_ptr<int16_t>  quats;
    iw3_ptr<float>    trans;
    iw3_ptr<char>     partClassification;
    iw3_ptr<DObjAnimMat> baseMat;
    iw3_ptr<XSurface> surfs;
    iw3_ptr<iw3_ptr<Material>> materialHandles;
    XModelLodInfo lodInfo[4];
    iw3_ptr<XModelCollSurf_s> collSurfs;
    int      numCollSurfs; int contents;
    iw3_ptr<XBoneInfo> boneInfo;
    float    radius; float mins[3]; float maxs[3];
    int16_t  numLods; int16_t collLod;
    XModelStreamInfo streamInfo;
    int      memUsage; char flags; bool bad;
    iw3_ptr<PhysPreset> physPreset;
    iw3_ptr<PhysGeomList> physGeoms;
};

// ===================================================================================================
// MAPENTS (Structs.hpp@1430) + the dynentity defs we extract from it.
// ===================================================================================================
struct TriggerModel { int contents; uint16_t hullCount; uint16_t firstHull; };
struct Bounds       { float midPoint[3]; float halfSize[3]; };
struct TriggerHull  { Bounds bounds; int contents; uint16_t slabCount; uint16_t firstSlab; };
struct TriggerSlab  { float dir[3]; float midPoint; float halfSize; };
struct MapTriggers {
    uint32_t count;     iw3_ptr<TriggerModel> models;
    uint32_t hullCount; iw3_ptr<TriggerHull>  hulls;
    uint32_t slabCount; iw3_ptr<TriggerSlab>  slabs;
};
struct GfxPlacement { float quat[4]; float origin[3]; };
struct DynEntityDef {
    int type; GfxPlacement pose;
    iw3_ptr<XModel> xModel;
    uint16_t brushModel; uint16_t physicsBrushModel;
    iw3_ptr<void> destroyFx; iw3_ptr<void> destroyPieces; iw3_ptr<PhysPreset> physPreset;
    int health; float mass[9]; int contents;  // PhysMass inlined (9 floats)
};
struct MapEnts {
    iw3_ptr<char> name;
    iw3_ptr<char> entityString;
    int           numEntityChars;
    MapTriggers   trigger;
    // (IW3 MapEnts continues with stages/dynents; the prototype dumps name+entityString+dynents only.)
};

// ===================================================================================================
// CLIPMAP (Structs.hpp@1466) — trimmed: head fields + mapEnts link the prototype reads. Deep
// collision arrays (planes/nodes/leafs/brushes/...) are typed iw3_ptr<void> (unwalked — null havok in
// IW8, SPEC §1). cmodel/comworld kept minimal.
// ===================================================================================================
struct cmodel_t { float mins[3]; float maxs[3]; float radius; iw3_ptr<void> info; int leaf; };

struct clipMap_t {
    iw3_ptr<char> name;
    int      isInUse;
    int      planeCount;            iw3_ptr<void> planes;
    uint32_t numStaticModels;       iw3_ptr<void> staticModelList;
    uint32_t numMaterials;          iw3_ptr<void> materials;
    uint32_t numBrushSides;         iw3_ptr<void> brushsides;
    uint32_t numBrushEdges;         iw3_ptr<void> brushEdges;
    uint32_t numNodes;              iw3_ptr<void> nodes;
    uint32_t numLeafs;              iw3_ptr<void> leafs;
    uint32_t leafbrushNodesCount;   iw3_ptr<void> leafbrushNodes;
    uint32_t numLeafBrushes;        iw3_ptr<void> leafbrushes;
    uint32_t numLeafSurfaces;       iw3_ptr<void> leafsurfaces;
    uint32_t vertCount;             iw3_ptr<void> verts;
    int      triCount;              iw3_ptr<void> triIndices;
    iw3_ptr<void> triEdgeIsWalkable;
    int      borderCount;           iw3_ptr<void> borders;
    int      partitionCount;        iw3_ptr<void> partitions;
    int      aabbTreeCount;         iw3_ptr<void> aabbTrees;
    uint32_t numSubModels;          iw3_ptr<cmodel_t> cmodels;
    uint16_t numBrushes;            iw3_ptr<void> brushes;
    int      numClusters; int clusterBytes; iw3_ptr<char> visibility; int vised;
    iw3_ptr<MapEnts> mapEnts;
    iw3_ptr<void> box_brush;
    cmodel_t box_model;
    uint16_t dynEntCount[2];
    iw3_ptr<DynEntityDef> dynEntDefList[2];
    iw3_ptr<void> dynEntPoseList[2];
    iw3_ptr<void> dynEntClientList[2];
    iw3_ptr<void> dynEntCollList[2];
    // (IW3 clipMap_t continues with checksum/numNodes etc.; prototype reads only the head + mapEnts.)
};

// ComWorld (IW3) — valid-empty in IW8, prototype reads name only.
struct ComWorld { iw3_ptr<char> name; int isInUse; /* primaryLights etc. (unwalked) */ };

#pragma pack(pop)

} // namespace iw3
