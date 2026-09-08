// maps_convert.h — IW3->IW8 conversion helpers for the MAPS asset family (map_ents/col_map/com_map
// + dynentitylist). Namespaced under convert:: to avoid clashes with other families' helpers.
//
// The single load-critical conversion is the entityString: IW3 (CoD4) serializes entity key/value
// pairs with STRING keys ("classname" "worldspawn"); IW8 (MW2019) serializes them with NUMERIC keyIds
// ({ 212 "worldspawn" }). The engine's spawn parser (G_SelectSpawnPoint / GScr_AddFieldsForEntity)
// reads the numeric keyId form. iw3ToIw8EntityString() reparses the IW3 text and re-emits the IW8
// numeric form, dropping keys we have no keyId for (and the entities that become empty).
//
// keyIds are the canonical IW8 entity-field ids confirmed from real shipped maps
// (fastfile_research/dumps/*.entityString.txt) and the proven iw8zonewriter mp_test_iw8_ents.txt:
// classname=212, origin=709, angles=80, etc.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace convert {

// Map an IW3 entity key string (lowercased) to its IW8 numeric keyId. Returns false if unknown
// (caller drops the pair). The table covers the keys the spawn/worldspawn flow needs; unknown keys are
// intentionally dropped rather than guessed (re-docs-dump-faithful: never invent keyIds).
bool iw3KeyToIw8KeyId(const std::string& iw3KeyLower, uint32_t& keyIdOut);

// Reparse an IW3 string-keyed entityString and emit the IW8 numeric-keyId entityString.
//   in  = the raw IW3 entityString text ({ "classname" "worldspawn" } ...), NUL not required.
//   out = the IW8 text ({ 212 "worldspawn" } ...). NO trailing NUL is added (the writer appends it and
//         sets numEntityChars = out.size()+1, matching map_zone.h emitMapEntsBody).
// Returns the number of entities emitted. Entities that lose ALL their keys (no known keyId) are
// dropped; worldspawn (classname worldspawn) is always preserved if present.
size_t iw3ToIw8EntityString(const std::string& in, std::string& out);

// ---- dynentitylist blob (binary, self-describing) -----------------------------------------------
// The dynentitylist0.dynent.data file carries the IW3 DynEntityDef[] extracted from the map (model +
// pose + physics) for downstream/inspection use. The LOAD-critical IW8 MapEnts body emits 0 dynents
// (the trailing dynent region zeroed = load-safe); these are recorded for fidelity / future work.
struct DynentRecord {
    int32_t  type;          // IW3 DynEntityType
    float    quat[4];       // GfxPlacement.quat
    float    origin[3];     // GfxPlacement.origin
    char     modelName[64]; // resolved XModel name (or "")
    uint16_t brushModel;
    int32_t  health;
    int32_t  contents;
};

// Serialize/parse the dynentitylist blob. Format = wrapBlob("DYNENT", 1, payload) where payload =
// u32 count followed by count * DynentRecord (POD, little-endian host write).
std::vector<uint8_t> serializeDynents(const std::vector<DynentRecord>& recs);
bool                 parseDynents(const std::vector<uint8_t>& blob, std::vector<DynentRecord>& out);

} // namespace convert
