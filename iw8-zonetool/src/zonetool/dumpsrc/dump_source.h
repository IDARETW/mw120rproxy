#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dumpsrc
{

// ---- map: ComWorld (maps/mp/<map>.d3dbsp.comworld, JSON) ---------------------------------------
struct ComPrimaryLight
{
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
struct ComWorldDump
{
    bool loaded = false;
    std::string name; // "maps/mp/<map>.d3dbsp"
    int isInUse = 0;
    int primaryLightCount = 0;
    std::vector<ComPrimaryLight> primaryLights;
};

// ---- map: ClipMap (maps/mp/<map>.d3dbsp.colmap, BinaryDumper) ----------------------------------
// For the IW8 converter only name + broadphase bounds + mapEnts-link are load-bearing (IW8
// collision is Havok, the IW3/5 brush data does NOT convert). We parse `name` always; we attempt
// the full walk to recover `verts` -> a TIGHT AABB; on ANY parse error we fall back to name-only
// and signal boundsFromVerts=false so the caller uses the proven generous AABB. mapEntsName == name
// (IW5 sets it).
struct ClipMapDump
{
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

// ---- map: entityString (maps/mp/<map>.d3dbsp.ents, raw text) + triggers -----------------------
struct TriggersDump
{
    bool loaded = false;
    int modelCount = 0, hullCount = 0, slabCount = 0; // mp_test: all 0
};
struct EntsDump
{
    bool loaded = false;
    std::string text; // raw CoD4 text-key entityString (verbatim from .ents)
    int numChars = 0;
    TriggersDump triggers;
};

class DumpSource
{
  public:
    DumpSource(const std::string &dumpDir, const std::string &mapName);

    const std::string &dir() const
    {
        return dir_;
    }
    const std::string &map() const
    {
        return map_;
    }

    // Quick existence checks (so the CLI can report what the dump contains).
    bool hasMapFiles() const;    // maps/mp/<map>.d3dbsp.{ents,colmap,comworld} present
    std::string mapBase() const; // "<dumpDir>/maps/mp/<map>.d3dbsp"

    // ---- map loaders (implemented) ------------------------------------------------------------
    EntsDump loadEnts() const;         // .ents (text) + .ents.triggers (bin)
    ComWorldDump loadComWorld() const; // .comworld (JSON)
    ClipMapDump loadClipMap() const;   // .colmap (+ .ents.stages) (BinaryDumper)

    // Enumerate asset names available in the dump.
    std::vector<std::string> listMaterials() const; // "<2char>/<name>"
    std::vector<std::string> listXModels() const;   // "<name>" (no .xme6)

  private:
    std::string dir_;
    std::string map_;
    std::string sub(const std::string &rel) const; // <dir>/<rel>
};

} // namespace dumpsrc
