

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dumpsrc {

// ---- TIER 1: ComWorld (maps/mp/<map>.d3dbsp.comworld, JSON) ---------------------------------------
struct ComPrimaryLight {
    int type = 0;
    int canUseShadowMap = 0;
    int exponent = 0;
    float color[3] = {0, 0, 0};
    float dir[3] = {0, 0, 0};
    float up[3] = {0, 0, 0};
    float origin[3] = {0, 0, 0};
    float radius = 0.f;
    float cosHalfFovOuter = 0.f, cosHalfFovInner = 0.f, cosHalfFovExpanded = 0.f;
    float rotationLimit = 0.f, translationLimit = 0.f;
    std::string defName;
};
struct ComWorldDump {
    bool loaded = false;
    std::string name; // "maps/mp/<map>.d3dbsp"
    int isInUse = 0;
    int primaryLightCount = 0;
    std::vector<ComPrimaryLight> primaryLights;
};

struct ClipMapDump {
    bool loaded = false; // name parsed OK (the minimum for a load-correct col_map)
    std::string name;    // "maps/mp/<map>.d3dbsp"
    int isInUse = 0;
    std::string mapEntsName;      // == name
    bool boundsFromVerts = false; // true if the AABB below was computed from real colmap verts
    float boundsMin[3] = {0, 0, 0};
    float boundsMax[3] = {0, 0, 0};
    int numVerts = 0;   // informational
    int stageCount = 0; // from .ents.stages (informational)
};

// ---- TIER 1: entityString (maps/mp/<map>.d3dbsp.ents, raw text) + triggers -----------------------
struct TriggersDump {
    bool loaded = false;
    int modelCount = 0, hullCount = 0, slabCount = 0; // mp_test: all 0
};
struct EntsDump {
    bool loaded = false;
    std::string text; // raw CoD4 text-key entityString (verbatim from .ents)
    int numChars = 0;
    TriggersDump triggers;
};

struct MaterialMapRef {
    std::string image;
    int semantic = 0;
    int sampleState = 0;
    uint32_t typeHash = 0;
};
struct MaterialDump {
    bool loaded = false;
    std::string name; // includes the 2-char prefix dir ("mc/mtl_...")
    std::string techniqueSetName;
    int gameFlags = 0, sortKey = 0, surfaceTypeBits = 0, stateFlags = 0, cameraRegion = 0;
    std::vector<MaterialMapRef> maps;
};
// XModel .xme6 (BinaryDumper): header counts + bone/material/lod-surface NAMES (IW5_DUMP_FORMAT.md §4).
struct XModelDump {
    bool loaded = false;
    std::string name;
    int numBones = 0, numRootBones = 0, numSurfaces = 0, numLods = 0, numColSurfs = 0;
    std::vector<std::string> boneNames;
    std::vector<std::string> materials; // [numSurfaces] material asset names
    std::vector<std::string>
        lodSurfaceNames; // [numLods] "zonetool_<model>_<lod>" -> the .xse files
    std::string physPresetName, physCollmapName;
};

struct XSurfaceDump {
    bool loaded = false;
    std::string name; // "zonetool_<model>_<lod>"
    int xSurficiesCount = 0;
};

struct GfxWorldDump {
    bool loaded = false;
    std::string name, baseName;
};

// ===================================================================================================
// DumpSource — opens a dump folder and exposes the typed loaders. All paths are derived from <dumpDir>
// + <map> per IW5_DUMP_FORMAT.md §0. Loaders are lazy + independent; each returns a *Dump with
// loaded=true on success. Tier-2 loaders currently return loaded=false (clearly marked).
// ===================================================================================================
class DumpSource {
  public:
    DumpSource(const std::string& dumpDir, const std::string& mapName);

    const std::string& dir() const {
        return dir_;
    }
    const std::string& map() const {
        return map_;
    }

    // Quick existence checks (so the CLI can report what the dump contains).
    bool hasMapFiles() const;    // maps/mp/<map>.d3dbsp.{ents,colmap,comworld} present
    std::string mapBase() const; // "<dumpDir>/maps/mp/<map>.d3dbsp"

    // ---- TIER 1 loaders (implemented) ------------------------------------------------------------
    EntsDump loadEnts() const;         // .ents (text) + .ents.triggers (bin)
    ComWorldDump loadComWorld() const; // .comworld (JSON)
    ClipMapDump loadClipMap() const;

    MaterialDump loadMaterial(const std::string& nameWithPrefix) const; // materials/<2char>/<name>
    XModelDump loadXModel(const std::string& name) const;               // XModel/<name>.xme6
    XSurfaceDump loadXSurface(const std::string& surfaceName) const;    // XSurface/<name>.xse
    GfxWorldDump loadGfxWorld() const;

    std::vector<std::string> listMaterials() const; // "<2char>/<name>"
    std::vector<std::string> listXModels() const;   // "<name>" (no .xme6)

  private:
    std::string dir_;
    std::string map_;
    std::string sub(const std::string& rel) const; // <dir>/<rel>
};

} // namespace dumpsrc
