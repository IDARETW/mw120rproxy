#include "replay_render.h"
#include "../../common/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
namespace replayrender
{
using namespace iw8;
template <class T> void put(uint8_t *b, size_t o, T v)
{
    std::memcpy(b + o, &v, sizeof(v));
}
template <class T> void append(std::vector<uint8_t> &b, T v)
{
    auto n = b.size();
    b.resize(n + sizeof(v));
    put(b.data(), n, v);
}
std::vector<uint8_t> unhex(const std::string &s)
{
    if (s.size() % 2 || s.size() > 131072)
        throw std::runtime_error("Invalid material byte data");
    std::vector<uint8_t> b;
    for (size_t i = 0; i < s.size(); i += 2)
    {
        auto digit = [](char c) -> unsigned {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            throw std::runtime_error("Invalid material hex digit");
        };
        b.push_back(uint8_t((digit(s[i]) << 4) | digit(s[i + 1])));
    }
    return b;
}
Material LoadMaterial(const std::string &path, const nlohmann::json &j)
{
    Material m;
    m.material = j.at("material").get<std::string>();
    const bool owned = m.material == "w/mw120r_test" ||
                       (m.material.starts_with("w/mw120r_mp_") && m.material.size() > 12 &&
                        m.material.size() <= 80 &&
                        std::all_of(m.material.begin() + 12, m.material.end(), [](char c) {
                            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
                        }));
    if (j.at("schema") != 1 || (m.material != "$default" && m.material != "mo/white_3d" && !owned))
        throw std::runtime_error("Unsupported Replay mesh schema/material");
    if (owned)
    {
        const auto file = j.at("materialDefinition").get<std::string>();
        if (std::filesystem::path(file).filename() != file || file.find("..") != std::string::npos)
            throw std::runtime_error("Material definition must be adjacent to mesh");
        std::ifstream mf(std::filesystem::path(path).parent_path() / file);
        const auto d = nlohmann::json::parse(mf);
        if (d.at("schema") != 1)
            throw std::runtime_error("Unknown material schema");
        m.materialInfo = unhex(d.at("info"));
        m.constants = unhex(d.at("constants"));
        m.bufferIndices = unhex(d.at("bufferIndices"));
        m.techset = d.at("techset");
        if (d.contains("techsetDefinition"))
        {
            const auto tf = d.at("techsetDefinition").get<std::string>();
            if (std::filesystem::path(tf).filename() != tf || tf.find("..") != std::string::npos)
                throw std::runtime_error("Techset definition must be adjacent");
            std::ifstream fts(std::filesystem::path(path).parent_path() / tf);
            const auto ts = nlohmann::json::parse(fts);
            m.techsetHeader = unhex(ts.at("header"));
            if (ts.at("schema") != 1 || ts.at("name") != m.techset || m.techsetHeader.size() != 64)
                throw std::runtime_error("Invalid Replay technique set");
            for (const auto &sh : ts.at("shaders"))
            {
                Shader s{sh.at("type"), sh.at("name"), sh.at("debugName"), unhex(sh.at("header")),
                         unhex(sh.at("program"))};
                if (s.type < 14 || s.type > 17 || s.header.size() != 40 || s.name.empty() ||
                    s.name.size() > 128 || s.debugName.size() > 128)
                    throw std::runtime_error("Invalid shader definition");
                for (unsigned i = 0; i < 32; ++i)
                    if (s.header[i])
                        throw std::runtime_error("Serialized shader pointer");
                uint32_t length;
                std::memcpy(&length, s.header.data() + 32, 4);
                if (length != s.program.size() ||
                    (length && (length < 4 || std::memcmp(s.program.data(), "DXBC", 4))))
                    throw std::runtime_error("Invalid DXBC program");
                m.shaders.push_back(s);
            }
            for (const auto &te : ts.at("techniques"))
            {
                Technique t;
                t.name = te.at("name");
                t.header = unhex(te.at("header"));
                t.states = unhex(te.at("states"));
                t.rootsig = unhex(te.at("rootsig"));
                t.statebits = unhex(te.at("statebits"));
                t.args = unhex(te.at("args"));
                if (t.header.size() != 184 || t.name.size() > 128 ||
                    t.states.size() != t.header[0x83] * t.header[0xE] * 16 ||
                    t.rootsig.size() != 16 || t.statebits.size() != t.header[0x83] * 5 ||
                    t.args.size() !=
                        (t.header[0x78] + t.header[0x79] + t.header[0x7A] + t.header[0x7B]) * 6)
                    throw std::runtime_error("Invalid native technique arrays");
                for (unsigned k = 0; k < 4; ++k)
                    if (!te.at("shaders")[k].is_null())
                        t.shaders[k] = te.at("shaders")[k];
                for (size_t off :
                     {0, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x88, 0xB0})
                    for (unsigned k = 0; k < 8; ++k)
                        if (t.header[off + k])
                            throw std::runtime_error("Serialized technique pointer");
                for (size_t i = 8; i < t.states.size(); i += 16)
                    for (unsigned k = 0; k < 8; ++k)
                        if (t.states[i + k])
                            throw std::runtime_error("Serialized PSO handle");
                m.techniques.push_back(t);
            }
            unsigned count = 0;
            for (size_t i = 24; i < 56; ++i)
                for (unsigned k = 0; k < 8; ++k)
                    count += (m.techsetHeader[i] >> k) & 1;
            if (count != m.techniques.size() || !count || count > 195)
                throw std::runtime_error("Technique mask/count mismatch");
            if (m.techset == "tw/mw120r_graybox_v1" ||
                (owned && m.techset.starts_with("tw/mw120r_mp_")))
            {
                if (m.techsetHeader[0x12] != 0x21 || m.materialInfo.size() != 32 ||
                    m.materialInfo[14] != 0x21)
                    throw std::runtime_error("Graybox material must use static-world geometry");
                for (const auto &t : m.techniques)
                {
                    // R_DrawBspSurf at Replay RVA 18F9460 dispatches only 32..38.
                    // Brush-model layouts 39..47 silently skip this draw path.
                    if (t.header[0x9C] < 32 || t.header[0x9C] > 38)
                        throw std::runtime_error("Shader layout is incompatible with static BSP");
                    for (unsigned k = 0; k < 4; ++k)
                        if (!t.shaders[k].empty() &&
                            std::none_of(m.shaders.begin(), m.shaders.end(), [&](const Shader &s) {
                                return s.type == 14 + k && s.name == t.shaders[k];
                            }))
                            throw std::runtime_error("Missing ordered shader definition");
                }
            }
        }
        if (m.materialInfo.size() != 32 || m.bufferIndices.size() != 195 ||
            (!m.techset.starts_with("w/lit_3_") && m.techset != "tw/mw120r_graybox_v1" &&
             !(owned && m.techset.starts_with("tw/mw120r_mp_") && !m.techniques.empty())) ||
            m.techset.size() > 200)
            throw std::runtime_error("Invalid Replay world material metadata");
        if (d.contains("imageDefinitions"))
            for (const auto &im : d.at("imageDefinitions"))
            {
                Image image;
                image.name = im.at("name");
                image.format = im.value("format", 7u);
                if (image.format != 6 && image.format != 7)
                    throw std::runtime_error(
                        "Resident RGBA8 image must use linear or sRGB RGBA8 format");
                const auto width = im.at("width").get<unsigned>(),
                           height = im.at("height").get<unsigned>();
                const auto pixels = im.at("rgba8").get<std::string>();
                if (!image.name.starts_with("mw120r/") || image.name.size() > 128 || !width ||
                    !height || width > 4096 || height > 4096 ||
                    std::filesystem::path(pixels).filename() != pixels ||
                    pixels.find("..") != std::string::npos)
                    throw std::runtime_error("Invalid resident Replay image definition");
                if (std::any_of(m.imageDefinitions.begin(), m.imageDefinitions.end(),
                                [&](const Image &x) { return x.name == image.name; }))
                    throw std::runtime_error("Duplicate Replay image definition");
                const auto pixelPath = std::filesystem::path(path).parent_path() / pixels;
                image.mipCount = im.value("mipCount", 1u);
                if (!image.mipCount || image.mipCount > 13)
                    throw std::runtime_error("Invalid resident mip count");
                size_t length = 0;
                unsigned mw = width, mh = height;
                for (unsigned level = 0; level < image.mipCount; ++level)
                {
                    length += size_t(mw) * mh * 4;
                    if (level + 1 < image.mipCount && mw == 1 && mh == 1)
                        throw std::runtime_error("Mip count exceeds dimensions");
                    mw = std::max(1u, mw / 2);
                    mh = std::max(1u, mh / 2);
                }
                if (std::filesystem::file_size(pixelPath) != length)
                    throw std::runtime_error("RGBA8 image length does not match dimensions");
                image.width = uint16_t(width);
                image.height = uint16_t(height);
                image.pixels.resize(length);
                std::ifstream input(pixelPath, std::ios::binary);
                if (!input.read(reinterpret_cast<char *>(image.pixels.data()), length))
                    throw std::runtime_error("Cannot read RGBA8 image pixels");
                m.imageDefinitions.push_back(std::move(image));
            }
        for (const auto &t : d.at("textures"))
        {
            auto h = unhex(t.at("header"));
            std::string image = t.at("image");
            if (h.size() != 8 ||
                (image != "$gray" && image != "$identitynormalmap" && image != "$black" &&
                 std::none_of(m.imageDefinitions.begin(), m.imageDefinitions.end(),
                              [&](const Image &x) { return x.name == image; })))
                throw std::runtime_error("Material texture has no image definition");
            m.textureHeaders.insert(m.textureHeaders.end(), h.begin(), h.end());
            m.images.push_back(image);
        }
        for (const auto &cb : d.at("buffers"))
        {
            if (cb.size() != 4)
                throw std::runtime_error("Expected four shader constant stages");
            std::array<std::vector<uint8_t>, 4> buffer;
            for (unsigned k = 0; k < 4; ++k)
                buffer[k] = unhex(cb[k]);
            m.buffers.push_back(buffer);
        }
        if (m.images.size() != m.materialInfo[0x14] ||
            m.constants.size() != m.materialInfo[0x15] * 20 ||
            m.buffers.size() != m.materialInfo[0x16] || m.materialInfo[0x17] ||
            m.materialInfo[0x18] || m.materialInfo[0x19])
            throw std::runtime_error("Material metadata counts do not match arrays");
    }
    return m;
}
Mesh Load(const std::string &path)
{
    Mesh m;
    if (path.empty())
        return m;
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot read Replay render mesh: " + path);
    const auto j = nlohmann::json::parse(f);
    static_cast<Material &>(m) = LoadMaterial(path, j);
    if (j.contains("additionalMaterials"))
        for (const auto &definition : j.at("additionalMaterials"))
        {
            if (m.additionalMaterials.size() >= 3)
                throw std::runtime_error("At most three additional materials are supported");
            m.additionalMaterials.push_back(LoadMaterial(path, definition));
        }
    const auto &list = j.at("surfaces");
    const unsigned atlasLayout = j.value("atlasVertexLayout", 1u);
    if (atlasLayout < 1 || atlasLayout > 3)
        throw std::runtime_error("Unsupported atlas vertex layout");
    if (!list.is_array() || list.empty() || list.size() > 4096)
        throw std::runtime_error("Invalid surface count");
    m.count = unsigned(list.size());
    m.surfaces.resize(m.count * 40);
    m.bounds.resize(m.count * 56);
    m.drawSurfs.resize(m.count * 16);
    m.surfData.resize(m.count * 88);
    // Native buffers reserve offset zero. Stock first surface starts at byte four.
    m.positions.resize(4);
    m.aux.resize(4);
    replaybounds::Accumulator sceneBounds, drawBounds;
    for (unsigned i = 0; i < m.count; ++i)
    {
        const auto &s = list[i];
        const auto &vertices = s.at("vertices");
        const auto &indices = s.at("indices");
        const unsigned material = s.value("materialIndex", 0u);
        if (material > m.additionalMaterials.size())
            throw std::runtime_error("Invalid surface material index");
        const bool sky = s.value("renderClass", std::string{}) == "sky";
        if (sky)
        {
            if (!material || vertices.empty() || !vertices[0].contains("lightmapUV") ||
                vertices[0]["lightmapUV"].size() != 2)
                throw std::runtime_error(
                    "Sky surfaces require a separate material and UV metadata");
            const float flags = vertices[0]["lightmapUV"][1].get<float>();
            if (!std::isfinite(flags) || flags < 0 || std::fmod(std::floor(flags), 4.0f) != 1)
                throw std::runtime_error("Invalid sky UV metadata");
        }
        const bool maskedPrepass =
            material && m.additionalMaterials[material - 1].material.ends_with("_foliage") &&
            std::any_of(m.additionalMaterials[material - 1].techniques.begin(),
                        m.additionalMaterials[material - 1].techniques.end(),
                        [](const Technique &t) {
                            uint32_t type = ~0u;
                            std::memcpy(&type, t.header.data() + 8, sizeof(type));
                            return type == 0;
                        });
        const bool opaque = !material || sky || maskedPrepass;
        if (opaque && m.opaqueCount != i)
            throw std::runtime_error("Opaque surfaces must precede transparent surfaces");
        if (opaque)
            ++m.opaqueCount;
        m.surfaceMaterials.push_back(material);
        if (vertices.size() < 3 || vertices.size() > 65535 || indices.empty() ||
            indices.size() % 3 || indices.size() / 3 > 65535)
            throw std::runtime_error("Invalid BSP mesh counts");
        float mins[3]{1e10f, 1e10f, 1e10f}, maxs[3]{-1e10f, -1e10f, -1e10f};
        const unsigned posOffset = unsigned(m.positions.size()),
                       normalOffset = unsigned(m.aux.size());
        for (const auto &v : vertices)
        {
            const auto &pos = v.at("position");
            if (pos.size() != 3)
                throw std::runtime_error("Invalid position");
            std::array<float, 3> point;
            for (unsigned k = 0; k < 3; ++k)
            {
                const float x = pos[k].get<float>();
                if (!std::isfinite(x) || std::abs(x) > 100000)
                    throw std::runtime_error("Position out of range");
                append(m.positions, x);
                point[k] = x;
                mins[k] = std::min(mins[k], x);
                maxs[k] = std::max(maxs[k], x);
            }
            drawBounds.Add(point);
            if (!sky)
                sceneBounds.Add(point);
            append(m.aux, v.at("normal").get<uint32_t>());
        }
        const unsigned uvOffset = unsigned(m.aux.size());
        for (const auto &v : vertices)
        {
            const auto &uv = v.at("uv");
            if (uv.size() != 2)
                throw std::runtime_error("Invalid UV");
            for (unsigned k = 0; k < 2; ++k)
            {
                const float x = uv[k].get<float>();
                if (!std::isfinite(x))
                    throw std::runtime_error("Nonfinite UV");
                append(m.aux, x);
            }
            if (atlasLayout >= 2)
            {
                if (!v.contains("lightmapUV") || v.at("lightmapUV").size() != 2)
                    throw std::runtime_error("Atlas metadata requires two components");
                for (unsigned k = 0; k < 2; ++k)
                {
                    const double value = v.at("lightmapUV").at(k).get<double>();
                    if (!std::isfinite(value) || value < 0 || value >= 65536)
                        throw std::runtime_error("Atlas metadata is outside supported range");
                    append(m.aux, float(std::floor(value)));
                }
                if (atlasLayout == 3)
                {
                    const auto parameters =
                        s.value("materialParameters", nlohmann::json::array({.8, 4.0, 2.5, .625}));
                    if (!parameters.is_array() || parameters.size() != 4)
                        throw std::runtime_error("Material parameters require four components");
                    for (const auto &parameter : parameters)
                    {
                        const float value = parameter.get<float>();
                        if (!std::isfinite(value) || std::abs(value) > 1e6f)
                            throw std::runtime_error(
                                "Material parameter is outside supported range");
                        append(m.aux, value);
                    }
                }
            }
        }
        const unsigned lmOffset = unsigned(m.aux.size());
        // Owned textured shaders may use this otherwise unused channel to select
        // an atlas tile. Preserve zeroes for the existing graybox packages.
        for (const auto &v : vertices)
            for (unsigned k = 0; k < 2; ++k)
            {
                double value =
                    v.contains("lightmapUV") ? v.at("lightmapUV").at(k).get<double>() : 0;
                if (atlasLayout >= 2)
                    value = (value - std::floor(value)) * 4 - 1;
                const float x = float(value);
                if (!std::isfinite(x))
                    throw std::runtime_error("Nonfinite lightmap UV");
                append(m.aux, x);
            }
        const unsigned colorOffset = unsigned(m.aux.size());
        for (const auto &v : vertices)
        {
            uint32_t rgba = 0xffffffff;
            if (v.contains("color"))
            {
                const auto &color = v.at("color");
                if (!color.is_array() || color.size() != 4)
                    throw std::runtime_error("Vertex color must contain four RGBA bytes");
                rgba = 0;
                for (unsigned k = 0; k < 4; ++k)
                {
                    if (!color[k].is_number_integer())
                        throw std::runtime_error("Vertex color component must be an integer");
                    const auto channel = color[k].get<int64_t>();
                    if (channel < 0 || channel > 255)
                        throw std::runtime_error("Vertex color component is outside RGBA8 range");
                    rgba |= uint32_t(channel) << (k * 8);
                }
            }
            append(m.aux, rgba);
        }
        const unsigned baseIndex = unsigned(m.indices.size() / 2);
        float maxEdge = 0;
        for (size_t ix = 0; ix < indices.size(); ++ix)
        {
            const unsigned a = indices[ix].get<unsigned>();
            if (a >= vertices.size())
                throw std::runtime_error("BSP index out of range");
            append(m.indices, uint16_t(a));
            const unsigned b = indices[ix - ix % 3 + (ix + 1) % 3].get<unsigned>();
            if (b >= vertices.size())
                throw std::runtime_error("BSP index out of range");
            float d = 0;
            for (unsigned k = 0; k < 3; ++k)
            {
                const float q = vertices[a]["position"][k].get<float>() -
                                vertices[b]["position"][k].get<float>();
                d += q * q;
            }
            maxEdge = std::max(maxEdge, std::sqrt(d));
        }
        auto *sf = m.surfaces.data() + 40 * i;
        put(sf, 0, posOffset);
        put(sf, 4, maxEdge);
        put(sf, 8, uint16_t(vertices.size()));
        put(sf, 10, uint16_t(indices.size() / 3));
        put(sf, 12, baseIndex);
        put(sf, 16, PTR_FOLLOWS);
        put(sf, 24, i);
        put(sf, 32, uint32_t(sky ? 0x40 : 0x41));
        auto *bounds = m.bounds.data() + 56 * i;
        for (unsigned k = 0; k < 3; ++k)
        {
            put(bounds, 4 * k, (mins[k] + maxs[k]) * 0.5f);
            put(bounds, 12 + 4 * k, (maxs[k] - mins[k]) * 0.5f + 1.0f);
        }
        auto *gpu = m.surfData.data() + 88 * i;
        put(gpu, 4, uint32_t(atlasLayout == 3 ? 4 : atlasLayout));
        put(gpu, 8, posOffset);
        put(gpu, 12, normalOffset);
        put(gpu, 16, lmOffset);
        put(gpu, 20, colorOffset);
        put(gpu, 24, uvOffset);
    }
    m.sceneBounds = sceneBounds.Finish();
    m.drawBounds = drawBounds.Finish();
    return m;
}
void StampWorld(std::vector<uint8_t> &w, const Mesh &m)
{
    if (!m.count)
        return;
    put(w.data(), 0xC8, m.count);
    put(w.data(), 0xCC, m.count);
    // Replay 18D19C0: opaque D0/D4, decal D8/DC, translucent E0/E4.
    for (size_t off = 0xD4; off <= 0xEC; off += 4)
        put(w.data(), off, m.count);
    for (size_t off : {0xD4, 0xD8, 0xDC, 0xE0})
        put(w.data(), off, m.opaqueCount);
    for (size_t off : {0xF0, 0xF8, 0x100, 0x108})
        put(w.data(), off, PTR_FOLLOWS);
    put(w.data(), 0x3F9C, m.words());
    // Per-view surface visibility bit arrays, native loader aligns each to 128.
    for (size_t off : {0x120, 0x128, 0x130, 0x138})
        put(w.data(), 0x3F98 + off, PTR_FOLLOWS);
    for (size_t off = 0x180; off <= 0x218; off += 8)
        put(w.data(), 0x3F98 + off, PTR_FOLLOWS);
    put(w.data(), 0x41E0, PTR_FOLLOWS); // sortedSurfaces[words*32], disk-backed uint32
    put(w.data(), 0x41F8, PTR_FOLLOWS); // surface casts-sun-shadow bits, runtime
}
void EmitMaterial(ZoneWriter &w, const Material &m, bool definition)
{
    w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    w.align(7);
    uint8_t material[0x78]{};
    put(material, 0, PTR_FOLLOWS);
    if (definition)
    {
        std::memcpy(material + 8, m.materialInfo.data(), 32);
        for (size_t o : {0x40, 0x48, 0x50, 0x60, 0x68})
            put(material, o, PTR_FOLLOWS);
    }
    w.write(material, sizeof(material));
    w.pushStream(XFILE_BLOCK_VIRTUAL);
    w.writeStr(((definition ? "" : ",") + m.material).c_str());
    if (definition)
    {
        w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
        w.align(7);
        uint8_t ts[64]{};
        put(ts, 0, PTR_FOLLOWS);
        w.write(ts, sizeof(ts));
        w.pushStream(XFILE_BLOCK_VIRTUAL);
        w.writeStr(("," + m.techset).c_str());
        w.popStream();
        w.popStream();
        w.align(7);
        for (size_t k = 0; k < m.images.size(); ++k)
        {
            w.write(m.textureHeaders.data() + k * 8, 8);
            w.writeT<uint64_t>(PTR_FOLLOWS);
        }
        for (const auto &image : m.images)
        {
            w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
            w.align(15);
            uint8_t im[0xE8]{};
            put(im, 0, PTR_FOLLOWS);
            w.write(im, sizeof(im));
            w.pushStream(XFILE_BLOCK_VIRTUAL);
            w.writeStr(("," + image).c_str());
            w.popStream();
            w.popStream();
        }
        w.align(15);
        w.write(m.constants.data(), m.constants.size());
        w.write(m.bufferIndices.data(), m.bufferIndices.size());
        w.align(15);
        for (const auto &cb : m.buffers)
        {
            uint8_t b[0x110]{};
            for (unsigned k = 0; k < 4; ++k)
                if (!cb[k].empty())
                {
                    put(b, 4 * k, uint32_t(cb[k].size()));
                    put(b, 16 + 8 * k, PTR_FOLLOWS);
                }
            w.write(b, sizeof(b));
        }
        for (const auto &cb : m.buffers)
            for (const auto &stage : cb)
                if (!stage.empty())
                {
                    w.align(15);
                    w.write(stage.data(), stage.size());
                }
    }
    w.popStream();
    w.popStream();
}
void RegisterMaterialDefinition(ZoneWriter &w, const Material &m,
                                std::set<std::pair<unsigned, std::string>> &registered)
{
    if (m.techset.empty())
        return;
    // Keep every dependency at the top level: Replay supports only two nested
    // asset patch-memory frames. Techniques themselves are not XAssets.
    for (const auto &image : m.imageDefinitions)
        if (registered.emplace(19, image.name).second)
            w.add(static_cast<IW8_XAssetType>(19), image.name, [image](ZoneWriter &out) {
                // Exact Replay Load_GfxImage E2FD20: 232 bytes, name in virtual,
                // resident pixel payload in TEMP_PRELOAD (E2FFF0), aligned to 16.
                // Image_LoadPixels 19387D0 creates the native GPU texture; no handles
                // or pointers from a captured process belong in this disk asset.
                out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
                out.align(15);
                uint8_t h[0xE8]{};
                put(h, 0, PTR_FOLLOWS);
                put(h, 0x14, uint32_t(image.format));
                put(h, 0x18,
                    uint32_t(image.mipCount > 1 ? 1 : 3)); // no picmip; bit 1 disables mipmapping
                put(h, 0x1C, uint32_t(image.pixels.size()));
                put(h, 0x24, image.width);
                put(h, 0x26, image.height);
                put(h, 0x28, uint16_t(1));
                put(h, 0x2A, uint16_t(1));
                h[0x2E] = 1;
                h[0x2F] = 1;
                h[0x30] = uint8_t(image.mipCount);
                put(h, 0xE0, PTR_FOLLOWS);
                out.write(h, sizeof(h));
                out.pushStream(XFILE_BLOCK_VIRTUAL);
                out.writeStr(image.name.c_str());
                out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
                out.align(15);
                out.write(image.pixels.data(), image.pixels.size());
                out.popStream();
                out.popStream();
                out.popStream();
                out.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
                out.align(15);
                out.reserveCalc(0xE8);
                out.popStream();
            });
    for (const auto &s : m.shaders)
        if (registered.emplace(s.type, s.name).second)
            w.add(static_cast<IW8_XAssetType>(s.type), s.name, [s](ZoneWriter &out) {
                out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
                out.align(7);
                auto h = s.header;
                put(h.data(), 0, PTR_FOLLOWS);
                if (!s.debugName.empty())
                    put(h.data(), 8, PTR_FOLLOWS);
                if (!s.program.empty())
                    put(h.data(), 24, PTR_FOLLOWS);
                out.write(h.data(), h.size());
                out.pushStream(XFILE_BLOCK_VIRTUAL);
                out.writeStr(s.name.c_str());
                if (!s.debugName.empty())
                    out.writeStr(s.debugName.c_str());
                if (!s.program.empty())
                {
                    out.align(3);
                    out.write(s.program.data(), s.program.size());
                }
                out.popStream();
                out.popStream();
                out.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
                out.align(7);
                out.reserveCalc(40);
                out.popStream();
            });
    if (!m.techniques.empty())
        w.add(ASSET_TYPE_TECHSET, m.techset, [m](ZoneWriter &out) {
            out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
            out.align(7);
            auto h = m.techsetHeader;
            put(h.data(), 0, PTR_FOLLOWS);
            put(h.data(), 56, PTR_FOLLOWS);
            out.write(h.data(), h.size());
            out.pushStream(XFILE_BLOCK_VIRTUAL);
            out.writeStr(m.techset.c_str());
            out.align(7);
            for (size_t index = 0; index < m.techniques.size(); ++index)
                out.writeT<uint64_t>(PTR_FOLLOWS);
            for (const auto &t : m.techniques)
            {
                auto b = t.header;
                put(b.data(), 0, PTR_FOLLOWS);
                for (size_t off : {0x28, 0x50, 0x88, 0xB0})
                    put(b.data(), off, PTR_FOLLOWS);
                for (unsigned k = 0; k < 4; ++k)
                    if (!t.shaders[k].empty())
                        put(b.data(), 0x58 + 8 * k, PTR_FOLLOWS);
                out.align(7);
                out.write(b.data(), b.size());
                out.writeStr(t.name.c_str());
                out.align(7);
                out.write(t.states.data(), t.states.size());
                out.write(t.rootsig.data(), t.rootsig.size());
                for (const auto &shader : t.shaders)
                    if (!shader.empty())
                    {
                        out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
                        out.align(7);
                        uint8_t sh[40]{};
                        put(sh, 0, PTR_FOLLOWS);
                        out.write(sh, sizeof(sh));
                        out.pushStream(XFILE_BLOCK_VIRTUAL);
                        out.writeStr(("," + shader).c_str());
                        out.popStream();
                        out.popStream();
                    }
                out.write(t.statebits.data(), t.statebits.size());
                out.align(1);
                out.write(t.args.data(), t.args.size());
            }
            out.popStream();
            out.popStream();
            out.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
            out.align(7);
            out.reserveCalc(64);
            for (const auto &t : m.techniques)
                for (const auto &s : t.shaders)
                    if (!s.empty())
                    {
                        out.align(7);
                        out.reserveCalc(40);
                    }
            out.popStream();
        });
    w.add(ASSET_TYPE_MATERIAL, m.material, [m](ZoneWriter &out) {
        EmitMaterial(out, m, true);
        out.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
        out.align(7);
        out.reserveCalc(120);
        out.align(7);
        out.reserveCalc(64);
        for (size_t index = 0; index < m.images.size(); ++index)
        {
            out.align(15);
            out.reserveCalc(0xE8);
        }
        out.popStream();
    });
}
void RegisterMaterial(ZoneWriter &w, const std::string &path)
{
    const auto m = Load(path);
    std::set<std::pair<unsigned, std::string>> registered;
    RegisterMaterialDefinition(w, m, registered);
    for (const auto &material : m.additionalMaterials)
        RegisterMaterialDefinition(w, material, registered);
}
void EmitSurfaces(ZoneWriter &w, const Mesh &m)
{
    if (!m.count)
        return;
    w.align(7);
    w.write(m.surfaces.data(), m.surfaces.size());
    for (unsigned i = 0; i < m.count; ++i)
    {
        // Native comma references resolve an existing material and its techset.
        // The stub is not installed as a replacement material.
        const unsigned material = m.surfaceMaterials[i];
        EmitMaterial(
            w, material ? m.additionalMaterials[material - 1] : static_cast<const Material &>(m),
            false);
    }
    w.align(3);
    w.write(m.bounds.data(), m.bounds.size());
    w.align(7);
    w.write(m.drawSurfs.data(), m.drawSurfs.size());
    w.align(63);
    w.write(m.surfData.data(), m.surfData.size());
}
void StampTransient(uint8_t *t, const Mesh &m)
{
    if (!m.count)
        return;
    put(t, 0x10, unsigned(m.positions.size()));
    put(t, 0x14, unsigned(m.aux.size()));
    put(t, 0x18, PTR_FOLLOWS);
    put(t, 0x20, PTR_FOLLOWS);
    put(t, 0xA8, unsigned(m.indices.size() / 2));
    put(t, 0xB0, PTR_FOLLOWS);
}
void EmitVertices(ZoneWriter &w, const Mesh &m)
{
    if (!m.count)
        return;
    w.align(3);
    w.write(m.positions.data(), m.positions.size());
    w.align(3);
    w.write(m.aux.data(), m.aux.size());
    w.align(3);
    w.write(m.indices.data(), m.indices.size());
}
void EmitSortedSurfaces(ZoneWriter &w, const Mesh &m)
{
    if (!m.count)
        return;
    w.align(3);
    for (unsigned i = 0; i < m.words() * 32; ++i)
        w.writeT<uint32_t>(i < m.count ? i : 0);
    w.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
    for (unsigned i = 0; i < m.count; ++i)
    {
        w.align(7);
        w.reserveCalc(0x78);
    }
    w.popStream();
    w.pushStream(XFILE_BLOCK_SHARED_STREAM);
    w.reserveCalc(0x4000);
    w.popStream();
}
} // namespace replayrender
