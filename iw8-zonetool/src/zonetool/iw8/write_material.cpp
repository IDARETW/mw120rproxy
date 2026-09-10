#include "common/log.h"
#include "dumpsrc/material_dumpsrc.h"
#include "iw8/iw8_zone.h"
#include <cstdint>
#include <cstring>

using namespace iw8_focus;
using namespace zt;

namespace convdump::mtl
{

namespace
{
template <typename T> void put(uint8_t *bytes, size_t offset, const T value)
{
    std::memcpy(bytes + offset, &value, sizeof(value));
}
} // namespace

bool writeMaterial(iw8::ZoneWriter &zw, const Iw8Material &im)
{
    if (!im.ok)
    {
        err("write_material: converted material not ok (name='%s')", im.name.c_str());
        return false;
    }

    // Register as a top-level material(11) asset. Capture `im` by value so the body owns its data.
    zw.add(ASSET_TYPE_MATERIAL, im.name, [im](iw8::ZoneWriter &w) {
        // ---- 1) the 0x78 Material struct with pointer fields stamped ----
        Material m = im.mat; // scalar fields already filled by convert()

        // Stamp pointer slots with RAW sentinels (host pointer fields are 8 bytes).
        m.name = reinterpret_cast<const char *>(iw8::PTR_FOLLOWS); // 0x00 follows
        m.packedAtlasData = nullptr;                               // 0x38 null
        m.techniqueSet = nullptr;                                  // 0x40 null (techset deferred)
        m.textureTable =
            im.textures.empty()
                ? nullptr
                : reinterpret_cast<MaterialTextureDef *>(iw8::PTR_FOLLOWS); // 0x48 follows
        m.constantTable =
            im.constants.empty()
                ? nullptr
                : reinterpret_cast<MaterialConstantDef *>(iw8::PTR_FOLLOWS); // 0x50 follows
        m.decalVolumeMaterial = nullptr;                                     // 0x58 null
        m.constantBufferIndex = nullptr;                                     // 0x60 null
        m.constantBufferTable = nullptr;                                     // 0x68 null
        m.subMaterials = im.subMaterials.empty()
                             ? nullptr
                             : reinterpret_cast<const char **>(iw8::PTR_FOLLOWS); // 0x70 follows

        // Defensive: keep the //CNT fields exactly in step with the emitted table lengths.
        m.textureCount = static_cast<uint8_t>(im.textures.size() & 0xFF);
        m.constantCount = static_cast<uint8_t>(im.constants.size() & 0xFF);
        m.layerCount = static_cast<uint8_t>(im.subMaterials.size() & 0xFF);

        static_assert(sizeof(Material) == 0x78, "IW8 Material must be 0x78 (g_assetSizes[11])");
        // Replay reads the 0x78 structure from TEMP_PRELOAD at 8-byte alignment. Its name and
        // tables follow in the virtual stream.
        w.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
        w.align(7);
        w.write(&m, sizeof(m));

        // ---- 2) followed payloads in STRUCT-FIELD ORDER -> VIRTUAL(8) ----
        w.pushStream(iw8::XFILE_BLOCK_VIRTUAL);

        // name (0x00) — XString
        w.writeStr(im.name.c_str());

        // textureTable (0x48): MaterialTextureDef[textureCount], 8-aligned (loader
        // FixStreamAlignment(7)).
        if (!im.textures.empty())
        {
            w.align(7);
            for (const auto &tb : im.textures)
            {
                MaterialTextureDef td{};
                std::memset(&td, 0, sizeof(td));
                td.index = tb.index; // 0x00 sampler/dest slot
                td.image = nullptr;  // 0x08 cross-asset GfxImage* -> NULL (load-safe)
                static_assert(sizeof(MaterialTextureDef) == 0x10,
                              "MaterialTextureDef must be 0x10");
                w.write(&td, sizeof(td));
            }
        }

        // constantTable (0x50): MaterialConstantDef[constantCount], stride 0x14, 16-aligned.
        if (!im.constants.empty())
        {
            w.align(15);
            for (const auto &c : im.constants)
            {
                static_assert(sizeof(MaterialConstantDef) == 0x14,
                              "MaterialConstantDef stride 0x14");
                w.write(&c, sizeof(c)); // all-scalar; no pointer to stamp
            }
        }

        // subMaterials (0x70): const char*[layerCount] (each element -2 follows) then each string,
        // 8-aligned. (None for the IW5 dump; emitted for completeness / future multi-layer
        // materials.)
        if (!im.subMaterials.empty())
        {
            w.align(7);
            for (size_t i = 0; i < im.subMaterials.size(); ++i)
                w.writeFollows();
            for (const auto &s : im.subMaterials)
                w.writeStr(s.c_str());
        }

        w.popStream(); // VIRTUAL(8)
        w.popStream(); // TEMP_PRELOAD(1)
    });

    info("write_material '%s': %zu tex, %zu const, %zu sub -> IW8 Material(0x78)", im.name.c_str(),
         im.textures.size(), im.constants.size(), im.subMaterials.size());
    return true;
}

bool writeCompassMaterial(iw8::ZoneWriter &zw, const std::string &name)
{
    if (name.empty())
    {
        return false;
    }

    zw.add(ASSET_TYPE_MATERIAL, name, [name](iw8::ZoneWriter &w) {
        constexpr const char *techset = "cust_2_2d_rpl_ta0_0_0_0_0_0";
        uint8_t material[0x78]{};
        put(material, 0x00, iw8::PTR_FOLLOWS);
        put(material, 0x08, uint32_t(1));
        put(material, 0x0C, uint32_t(65536));
        material[0x18] = 3;
        material[0x19] = 30;
        material[0x1C] = 1;
        material[0x1D] = 1;
        material[0x1E] = 1;
        material[0x22] = 1;
        material[0x23] = 1;
        for (const size_t offset : {0x40, 0x48, 0x50, 0x60, 0x68})
        {
            put(material, offset, iw8::PTR_FOLLOWS);
        }

        w.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
        w.align(7);
        w.write(material, sizeof(material));
        w.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
        w.writeStr(name);

        w.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
        w.align(7);
        uint8_t techniqueSet[64]{};
        put(techniqueSet, 0, iw8::PTR_FOLLOWS);
        w.write(techniqueSet, sizeof(techniqueSet));
        w.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
        w.writeStr(std::string(",") + techset);
        w.popStream();
        w.popStream();

        w.align(7);
        uint8_t texture[16]{};
        texture[0] = 158;
        put(texture, 8, iw8::PTR_FOLLOWS);
        w.write(texture, sizeof(texture));
        w.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
        w.align(15);
        uint8_t image[0xE8]{};
        put(image, 0, iw8::PTR_FOLLOWS);
        w.write(image, sizeof(image));
        w.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
        w.writeStr(std::string(",") + name);
        w.popStream();
        w.popStream();

        w.align(15);
        uint8_t constant[20]{};
        constant[0] = 126;
        for (size_t component = 0; component < 4; ++component)
        {
            put(constant, 4 + component * sizeof(float), 1.0f);
        }
        w.write(constant, sizeof(constant));

        uint8_t bufferIndices[195];
        std::memset(bufferIndices, 0xFF, sizeof(bufferIndices));
        bufferIndices[30] = 0;
        w.write(bufferIndices, sizeof(bufferIndices));

        w.align(15);
        // Replay's load routine consumes the full runtime-backed 0x110 definition. The
        // serializable fields occupy the first 0x30 bytes; the remaining buffer handles stay
        // zero for the loader to create.
        uint8_t constantBuffer[0x110]{};
        put(constantBuffer, 12, uint32_t(16));
        put(constantBuffer, 40, iw8::PTR_FOLLOWS);
        w.write(constantBuffer, sizeof(constantBuffer));
        w.align(15);
        const float pixelConstants[4]{1.0f, 1.0f, 1.0f, 1.0f};
        w.write(pixelConstants, sizeof(pixelConstants));

        w.popStream();
        w.popStream();

        w.pushStream(iw8::XFILE_BLOCK_TEMP_POSTLOAD);
        w.align(7);
        w.reserveCalc(sizeof(material));
        w.align(7);
        w.reserveCalc(sizeof(techniqueSet));
        w.align(15);
        w.reserveCalc(sizeof(image));
        w.popStream();
    });
    info("write_material: native compass material '%s'", name.c_str());
    return true;
}

// One-shot read -> convert -> register-for-write (the caller's build-map entry point per material).
bool emitMaterialFromDump(iw8::ZoneWriter &zw, const std::string &filePath,
                          const std::string &displayName)
{
    DumpMaterial d = readMaterialJson(filePath, displayName);
    if (!d.loaded)
        return false;
    Iw8Material im = convert(d);
    if (!im.ok)
    {
        err("emitMaterialFromDump: convert failed for '%s'", filePath.c_str());
        return false;
    }
    return writeMaterial(zw, im);
}

} // namespace convdump::mtl
