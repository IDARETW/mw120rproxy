

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace iw3sr {

// One manifest row. `type` is an IW8 asset-type name ("material","image","xmodel","xmodelsurfs",
// "col_map","com_map","map_ents","gfx_map","glass_map",...). `name` is the asset name (no extension).
struct ManifestEntry {
    std::string type; // IW8 type name (lowercase)
    std::string name; // asset name
};

class ZoneSource {
  public:
    // Open a zone-source rooted at `assetDir` for the given `mapName` (used for the manifest filename
    // and the maps/mp/<map>.d3dbsp.* sub-asset paths). `create` mkdirs the standard subfolders (Stage
    // A); when false (Stage B) the dir is expected to already exist.
    ZoneSource(const std::string& assetDir, const std::string& mapName, bool create);

    const std::string& dir() const {
        return dir_;
    }
    const std::string& map() const {
        return map_;
    }

    // ---- manifest (CSV) --------------------------------------------------------------------------
    // Stage A: record an asset in the manifest (type = IW8 type name). Order is preserved = zone order.
    void manifestAdd(const std::string& iw8Type, const std::string& name);
    // Stage A: flush the manifest to <assetDir>/<map>.csv. Returns false on write failure.
    bool manifestWrite();
    // Stage B: load the manifest from disk into entries(). Returns false if missing/unreadable.
    bool manifestRead();
    const std::vector<ManifestEntry>& entries() const {
        return entries_;
    }

    // ---- raw payload helpers (used by typed wrappers + per-asset code) ----------------------------
    // Write/read an arbitrary binary payload at a path RELATIVE to the asset dir (e.g.
    // "images/foo.iwi"). Creates parent dirs on write. Returns success.
    bool writeBlob(const std::string& relPath, const void* data, size_t n);
    bool writeBlob(const std::string& relPath, const std::vector<uint8_t>& data);
    bool readBlob(const std::string& relPath, std::vector<uint8_t>& out) const;
    // Text payloads (JSON, entityString).
    bool writeText(const std::string& relPath, const std::string& text);
    bool readText(const std::string& relPath, std::string& out) const;
    bool blobExists(const std::string& relPath) const;

    std::string imagePath(const std::string& name) const;    // images/<name>.iwi
    std::string materialPath(const std::string& name) const; // materials/<name>.json
    std::string xmodelPath(const std::string& name) const;   // xmodel/<name>.json
    std::string xsurfacePath(const std::string& name) const; // xsurface/<name>.xsurf_bin
    std::string mapSubPath(const std::string& sub) const;    // maps/mp/<map>.d3dbsp.<sub>
    std::string dynentPath() const;                          // dynentitylist0.dynent.data

    // ---- typed convenience (image/material/xmodel/xsurface/map) -----------------------------------
    // Stage A "add" = manifestAdd + writeBlob/writeText in one call; Stage B "get" = readBlob/readText.
    // (Bodies are thin; per-asset code may also use the raw helpers directly.)
    bool addImage(const std::string& name, const std::vector<uint8_t>& iwiBytes);
    bool getImage(const std::string& name, std::vector<uint8_t>& out) const;
    bool addMaterial(const std::string& name, const std::string& json);
    bool getMaterial(const std::string& name, std::string& out) const;
    bool addXModel(const std::string& name, const std::string& json);
    bool getXModel(const std::string& name, std::string& out) const;
    bool addXSurface(const std::string& name, const std::vector<uint8_t>& binBytes);
    bool getXSurface(const std::string& name, std::vector<uint8_t>& out) const;

    // Map sub-assets (entityString text + struct JSON). `sub` ∈ {entityString, map_ents, col_map,
    // com_map, gfx_map, glass_map}. add* also records the IW8 type in the manifest when `iw8Type` set.
    bool addMapSubText(const std::string& iw8Type, const std::string& sub, const std::string& text);
    bool getMapSubText(const std::string& sub, std::string& out) const;

    // Dynentity blob (binary, extracted from MapEnts).
    bool writeDynents(const std::vector<uint8_t>& data);
    bool readDynents(std::vector<uint8_t>& out) const;

    static std::vector<uint8_t>
    wrapBlob(const char* magic, uint32_t version, const void* payload, size_t n);
    static bool unwrapBlob(const std::vector<uint8_t>& blob,
                           std::string& magicOut,
                           uint32_t& versionOut,
                           std::vector<uint8_t>& payloadOut);

  private:
    std::string dir_;
    std::string map_;
    std::vector<ManifestEntry> entries_;

    std::string manifestPath() const;              // <dir>/<map>.csv
    std::string abs(const std::string& rel) const; // <dir>/<rel>
};

} // namespace iw3sr
