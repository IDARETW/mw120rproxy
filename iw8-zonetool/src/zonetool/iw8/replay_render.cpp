#include "replay_render.h"
#include "../../common/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <optional>
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

namespace
{
bool nativeEffectTechset(const std::string_view name)
{
    return name ==
               "elcq/unlit_6_effect_bad_ta0_802_4_0_1_0_0_0_100000023_0_0_2_0_0" ||
           name ==
               "elcq/unlit_6_effect_bad_ta0_802_1004_0_1_0_0_0_100000023_0_0_2_0_0" ||
           name ==
               "elcq/unlit_6_effect_bad_tca_802_10a4_0_1_0_0_0_100060023_0_0_3_0_0" ||
           name ==
               "elcq/unlit_6_effect_bad_tca_802_4_0_1_0_0_0_100060023_0_0_3_0_0" ||
           name ==
               "elcq/unlit_6_effect_bad_ta0_802_4_0_1_0_0_0_100020023_0_0_2_0_0";
}

bool nativeSimpleAlphaEffectTechset(const std::string_view name)
{
    return name == "elcq/unlit_6_effect_bad_ta0_802_4_0_1_0_0_0_100000023_0_0_2_0_0";
}

bool nativeCloudEffectTechset(const std::string_view name)
{
    return name == "elcq/unlit_6_effect_bad_ta0_802_4_0_1_0_0_0_100020023_0_0_2_0_0";
}

bool nativeSimpleAdditiveEffectTechset(const std::string_view name)
{
    return name == "elcq/unlit_6_effect_bad_tca_802_4_0_1_0_0_0_100060023_0_0_3_0_0";
}

std::optional<uint8_t> generatedTextureSlot(const char *name)
{
    if (!std::strcmp(name, "sourceAtlas"))
        return uint8_t{0};
    if (!std::strcmp(name, "sourceNormalAtlas"))
        return uint8_t{9};
    if (!std::strcmp(name, "sourceResponseAtlas"))
        return uint8_t{59};
    return std::nullopt;
}

bool hasMaterialTextureSlot(const std::vector<uint8_t> &headers, const uint8_t slot)
{
    unsigned matches = 0;
    for (size_t offset = 0; offset < headers.size(); offset += 8)
        if (headers[offset] == slot)
            ++matches;
    return matches == 1;
}

bool hasPixelTextureArgument(const std::vector<uint8_t> &args, const uint8_t registerIndex,
                             const uint8_t materialSlot)
{
    for (size_t offset = 0; offset < args.size(); offset += 6)
        if (args[offset] == 5 && args[offset + 1] == 0x10 && args[offset + 2] == registerIndex &&
            args[offset + 3] == 0 && args[offset + 4] == materialSlot && args[offset + 5] == 0)
            return true;
    return false;
}

bool hasCodeTextureArgument(const std::vector<uint8_t> &args, const uint8_t stageMask,
                            const uint8_t destination, const uint16_t codeImage)
{
    for (size_t offset = 0; offset < args.size(); offset += 6)
        if (args[offset] == 8 && args[offset + 1] == stageMask &&
            args[offset + 2] == destination && args[offset + 3] == 0 &&
            args[offset + 4] == (codeImage & 0xff) && args[offset + 5] == (codeImage >> 8))
            return true;
    return false;
}

void validateGeneratedTextureBindings(const Material &material, const bool staticModel)
{
    for (const auto &technique : material.techniques)
    {
        if (technique.shaders[3].empty())
            continue;
        const auto shader = std::find_if(
            material.shaders.begin(), material.shaders.end(), [&](const Shader &candidate) {
                return candidate.type == 17 && candidate.name == technique.shaders[3];
            });
        if (shader == material.shaders.end() || shader->program.empty())
            continue;

        ID3D11ShaderReflection *reflection = nullptr;
        if (FAILED(D3DReflect(shader->program.data(), shader->program.size(),
                              __uuidof(ID3D11ShaderReflection),
                              reinterpret_cast<void **>(&reflection))) ||
            !reflection)
            throw std::runtime_error("Cannot reflect generated pixel shader " + shader->name);
        D3D11_SHADER_DESC description{};
        const HRESULT descriptionResult = reflection->GetDesc(&description);
        if (FAILED(descriptionResult))
        {
            reflection->Release();
            throw std::runtime_error("Cannot read generated pixel shader reflection " +
                                     shader->name);
        }
        uint32_t techniqueType{};
        std::memcpy(&techniqueType, technique.header.data() + 8, sizeof(techniqueType));
        const bool litForwardPlus = techniqueType == 34;
        unsigned gtaoImageCount = 0;
        unsigned gtaoSamplerCount = 0;
        unsigned sunVisibilityCount = 0;
        unsigned lightingTilesCount = 0;
        for (unsigned resourceIndex = 0; resourceIndex < description.BoundResources;
             ++resourceIndex)
        {
            D3D11_SHADER_INPUT_BIND_DESC binding{};
            if (FAILED(reflection->GetResourceBindingDesc(resourceIndex, &binding)))
            {
                reflection->Release();
                throw std::runtime_error("Cannot read generated pixel texture binding " +
                                         shader->name);
            }
            const std::string resourceName = binding.Name ? binding.Name : "";
            if (resourceName == "sunVisibility")
            {
                ++sunVisibilityCount;
                if (binding.Type != D3D_SIT_TEXTURE || binding.BindPoint != 85 ||
                    binding.BindCount != 1)
                {
                    reflection->Release();
                    throw std::runtime_error("Generated sun visibility is not bound to t85: " +
                                             technique.name);
                }
                continue;
            }
            if (resourceName == "lightingTiles")
            {
                ++lightingTilesCount;
                const unsigned expectedRegister = staticModel ? 4 : 5;
                if (binding.Type != D3D_SIT_STRUCTURED ||
                    binding.BindPoint != expectedRegister || binding.BindCount != 1)
                {
                    reflection->Release();
                    throw std::runtime_error(
                        "Generated sun tile classifications use the wrong Replay binding: " +
                        technique.name);
                }
                continue;
            }
            if (resourceName == "gtaoImage")
            {
                ++gtaoImageCount;
                const uint8_t stageMask = staticModel ? 0x12 : 0x10;
                if (binding.Type != D3D_SIT_TEXTURE || binding.BindPoint != 95 ||
                    binding.BindCount != 1 ||
                    !hasCodeTextureArgument(technique.args, stageMask, 7, 0))
                {
                    reflection->Release();
                    throw std::runtime_error(
                        "Generated GTAO image is not bound to native code image 0 at t95: " +
                        technique.name);
                }
                continue;
            }
            if (resourceName == "gtaoSampler")
            {
                ++gtaoSamplerCount;
                if (binding.Type != D3D_SIT_SAMPLER || binding.BindPoint != 8 ||
                    binding.BindCount != 1)
                {
                    reflection->Release();
                    throw std::runtime_error("Generated GTAO sampler is not bound to s8: " +
                                             technique.name);
                }
                continue;
            }
            const auto materialSlot = generatedTextureSlot(resourceName.c_str());
            if (!materialSlot || binding.Type != D3D_SIT_TEXTURE)
                continue;
            if (!hasMaterialTextureSlot(material.textureHeaders, *materialSlot))
            {
                reflection->Release();
                throw std::runtime_error("Generated pixel texture semantic is not bound: " +
                                         resourceName + " in " + technique.name);
            }
            if (binding.BindPoint > UINT8_MAX ||
                !hasPixelTextureArgument(technique.args, static_cast<uint8_t>(binding.BindPoint),
                                         *materialSlot))
            {
                reflection->Release();
                throw std::runtime_error("Generated pixel texture has no native type-5 argument: " +
                                         resourceName + " in " + technique.name);
            }
        }
        if (litForwardPlus &&
            (gtaoImageCount != 1 || gtaoSamplerCount != 1 || sunVisibilityCount != 1 ||
             lightingTilesCount != 1))
        {
            reflection->Release();
            throw std::runtime_error(
                "Generated lit pass must have native GTAO and sun-shadow bindings: " +
                technique.name);
        }
        reflection->Release();
    }
}
} // namespace

Image LoadImageDefinition(const std::string &path, const nlohmann::json &source,
                          const uint32_t expectedMapType)
{
    const bool cubemap = expectedMapType == 0x8000u;
    const bool array = expectedMapType == 0x20000u;
    Image image;
    image.name = source.at("name");
    image.format = source.value("format", 7u);
    image.flags = source.value("flags", cubemap ? 0x8001u : 0u);
    const auto depth = source.value("depth", 1u);
    const auto numElements = source.value("numElements", 1u);
    const auto semantic = source.value("semantic", 1u);
    const auto category = source.value("category", 1u);
    image.mipCount = source.value("mipCount", 1u);
    const auto width = source.at("width").get<unsigned>();
    const auto height = source.at("height").get<unsigned>();
    const bool bc6h = image.format == 42;
    const char *pixelField = bc6h ? "bc6h" : "rgba8";
    const auto pixels = source.at(pixelField).get<std::string>();
    const uint32_t mapType = image.flags & 0x38000u;
    if (!image.name.starts_with("mw120r/") || image.name.size() > 128 || !width || !height ||
        width > 4096 || height > 4096 || !depth || !numElements || depth > 4096 ||
        numElements > 2048 || semantic > UINT8_MAX || category > UINT8_MAX || !image.mipCount ||
        image.mipCount > 13 || (image.format != 6 && image.format != 7 && !bc6h) ||
        mapType != expectedMapType || std::filesystem::path(pixels).filename() != pixels ||
        pixels.find("..") != std::string::npos)
        throw std::runtime_error("Invalid resident Replay image definition");
    if (cubemap && (width != height || depth != 1 || numElements != 1))
        throw std::runtime_error("Replay reflection image must be one square cubemap");
    if (array && (width != height || depth != 1 || numElements < 1))
        throw std::runtime_error("Replay reflection array must contain square 2D images");
    if (!cubemap && !array && numElements != 1)
        throw std::runtime_error("Replay 2D image has an array element count");

    size_t length = 0;
    unsigned mipWidth = width, mipHeight = height;
    const size_t slices = cubemap ? 6u : numElements;
    for (unsigned level = 0; level < image.mipCount; ++level)
    {
        const size_t subresource =
            bc6h ? size_t((mipWidth + 3) / 4) * ((mipHeight + 3) / 4) * 16
                 : size_t(mipWidth) * mipHeight * 4;
        if (subresource > (SIZE_MAX - 15) ||
            ((subresource + 15) & ~size_t{15}) > (SIZE_MAX - length) / slices)
            throw std::runtime_error("Resident Replay image allocation exceeds address space");
        length += ((subresource + 15) & ~size_t{15}) * slices;
        if (level + 1 < image.mipCount && mipWidth == 1 && mipHeight == 1)
            throw std::runtime_error("Mip count exceeds dimensions");
        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);
    }
    const auto pixelPath = std::filesystem::path(path).parent_path() / pixels;
    std::error_code error;
    if (length > UINT32_MAX || !std::filesystem::is_regular_file(pixelPath, error) ||
        std::filesystem::file_size(pixelPath) != length)
        throw std::runtime_error(std::string(bc6h ? "BC6H" : "RGBA8") +
                                 " image length does not match dimensions: " + image.name);
    image.width = static_cast<uint16_t>(width);
    image.height = static_cast<uint16_t>(height);
    image.depth = static_cast<uint16_t>(depth);
    image.numElements = static_cast<uint16_t>(numElements);
    image.semantic = static_cast<uint8_t>(semantic);
    image.category = static_cast<uint8_t>(category);
    image.pixels.resize(length);
    std::ifstream input(pixelPath, std::ios::binary);
    if (!input.read(reinterpret_cast<char *>(image.pixels.data()), length))
        throw std::runtime_error(std::string("Cannot read ") + (bc6h ? "BC6H" : "RGBA8") +
                                 " image pixels");
    return image;
}

Material LoadMaterial(const std::string &path, const nlohmann::json &j,
                      const bool auxiliaryEffect = false)
{
    Material m;
    m.material = j.at("material").get<std::string>();
    constexpr std::string_view effectPrefix = "elcq/mw120r_fx_";
    const bool ownedEffect = auxiliaryEffect && m.material.starts_with(effectPrefix) &&
                             m.material.size() <= 128 &&
                             std::all_of(m.material.begin() + effectPrefix.size(),
                                         m.material.end(), [](char c) {
                                         return (c >= 'a' && c <= 'z') ||
                                                (c >= '0' && c <= '9') || c == '_';
                             });
    const bool owned = ownedEffect || m.material == "w/mw120r_test" ||
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
        bool nativeReplayFixture = false;
        m.decalVolumeMaterial = d.value("decalVolumeMaterial", std::string{});
        const bool hasOwnedDecalDefinition = d.contains("decalVolumeDefinition");
        if (hasOwnedDecalDefinition)
        {
            const auto &volume = d.at("decalVolumeDefinition");
            auto &target = m.decalVolumeDefinition;
            target.name = volume.at("name").get<std::string>();
            const auto &channels = volume.at("channels");
            if (!channels.is_array() || channels.size() != target.channels.size())
                throw std::runtime_error("Replay decal volume must define six image channels");
            for (size_t channel = 0; channel < target.channels.size(); ++channel)
                if (!channels[channel].is_null())
                    target.channels[channel] = channels[channel].get<std::string>();
            target.flags = volume.at("flags").get<uint32_t>();
            const auto tint = volume.at("colorTint").get<std::vector<float>>();
            if (tint.size() != target.colorTint.size())
                throw std::runtime_error("Replay decal volume tint must contain three values");
            std::copy(tint.begin(), tint.end(), target.colorTint.begin());
            const unsigned rows = volume.value("rows", 1u);
            const unsigned columns = volume.value("columns", 1u);
            if (!rows || rows > UINT8_MAX || !columns || columns > UINT8_MAX)
                throw std::runtime_error("Replay decal volume atlas dimensions are invalid");
            target.rows = static_cast<uint8_t>(rows);
            target.columns = static_cast<uint8_t>(columns);
        }
        constexpr std::string_view generatedDecalPrefix = "i/mw120r_fx_decal_";
        const bool generatedDecalName = m.decalVolumeDefinition.name.starts_with(
                                            generatedDecalPrefix) &&
                                        m.decalVolumeDefinition.name.size() <= 96 &&
                                        std::all_of(m.decalVolumeDefinition.name.begin() +
                                                        generatedDecalPrefix.size(),
                                                    m.decalVolumeDefinition.name.end(), [](char c) {
                                            return (c >= 'a' && c <= 'z') ||
                                                   (c >= '0' && c <= '9') || c == '_';
                                        });
        const bool ownedDecalCarrier = ownedEffect && m.techset == "null" &&
                                       (m.decalVolumeMaterial ==
                                            "i/vfx_decal_surface_glass_2" ||
                                        (hasOwnedDecalDefinition && generatedDecalName &&
                                         m.decalVolumeDefinition.name ==
                                             m.decalVolumeMaterial));
        const bool nativeEffectAlias = ownedEffect && nativeEffectTechset(m.techset) &&
                                       !d.contains("techsetDefinition");
        if (d.contains("techsetDefinition"))
        {
            const auto tf = d.at("techsetDefinition").get<std::string>();
            if (std::filesystem::path(tf).filename() != tf || tf.find("..") != std::string::npos)
                throw std::runtime_error("Techset definition must be adjacent");
            std::ifstream fts(std::filesystem::path(path).parent_path() / tf);
            const auto ts = nlohmann::json::parse(fts);
            nativeReplayFixture = ts.value("nativeReplayFixture", false);
            m.techsetHeader = unhex(ts.at("header"));
            if (ts.at("schema") != 1 || ts.at("name") != m.techset || m.techsetHeader.size() != 64)
                throw std::runtime_error("Invalid Replay technique set");
            if (nativeReplayFixture &&
                (!owned || m.techset.find("_native_world_cull") == std::string::npos))
                throw std::runtime_error("Invalid native Replay world technique fixture");
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
                uint32_t materialType{}, techsetType{};
                if (m.materialInfo.size() != 32)
                    throw std::runtime_error("Generated material metadata is incomplete");
                std::memcpy(&materialType, m.materialInfo.data() + 0xC, sizeof(materialType));
                std::memcpy(&techsetType, m.techsetHeader.data() + 0x10, sizeof(techsetType));
                const bool staticWorld = materialType == 0x210000u && techsetType == 0x210000u;
                const bool staticModel = materialType == 589844u && techsetType == 589844u;
                const bool glass = materialType == 0x100000u && techsetType == 0x100000u;
                if (!staticWorld && !staticModel && !glass)
                    throw std::runtime_error(
                        "Generated material has an incompatible geometry type");
                for (const auto &t : m.techniques)
                {
                    const uint8_t layout = t.header[0x9C];
                    uint32_t techniqueType{};
                    std::memcpy(&techniqueType, t.header.data() + 8, sizeof(techniqueType));
                    const uint8_t nativeStaticLayout = techniqueType == 34 ? 2 : 1;
                    if ((staticWorld && (layout < 32 || layout > 38)) ||
                        (staticModel && layout != nativeStaticLayout) ||
                        (glass && layout != 31))
                        throw std::runtime_error("Shader layout is incompatible with its geometry");
                    if (glass && t.header[0x0F + 1] >= t.header[0x0E])
                        throw std::runtime_error("Glass vertex declaration has no native pipeline state");
                    for (unsigned k = 0; k < 4; ++k)
                        if (!t.shaders[k].empty() &&
                            std::none_of(m.shaders.begin(), m.shaders.end(), [&](const Shader &s) {
                                return s.type == 14 + k && s.name == t.shaders[k];
                            }))
                            throw std::runtime_error("Missing ordered shader definition");
                }
            }
            if (ownedEffect && m.techset.starts_with("tw/mw120r_fx_"))
            {
                uint32_t materialType{}, techsetType{};
                if (m.materialInfo.size() != 32)
                    throw std::runtime_error("Replay effect material metadata is incomplete");
                std::memcpy(&materialType, m.materialInfo.data() + 0xC, sizeof(materialType));
                std::memcpy(&techsetType, m.techsetHeader.data() + 0x10, sizeof(techsetType));
                if (materialType != 0x40200Cu || techsetType != materialType)
                    throw std::runtime_error("Replay effect material must use an effect-quad techset");
                for (const auto &t : m.techniques)
                {
                    if (t.header[0x9C] != 0)
                        throw std::runtime_error("Replay effect shader layout is not an effect quad");
                    for (unsigned k = 0; k < 4; ++k)
                        if (!t.shaders[k].empty() &&
                            std::none_of(m.shaders.begin(), m.shaders.end(), [&](const Shader &s) {
                                return s.type == 14 + k && s.name == t.shaders[k];
                            }))
                            throw std::runtime_error("Missing ordered Replay effect shader");
                }
            }
        }
        if (ownedEffect && nativeEffectAlias)
        {
            uint32_t materialType{};
            if (m.materialInfo.size() != 32)
                throw std::runtime_error("Replay effect material metadata is incomplete");
            std::memcpy(&materialType, m.materialInfo.data() + 0xC, sizeof(materialType));
            const bool simpleAdditive = nativeSimpleAdditiveEffectTechset(m.techset);
            const bool oneBuffer = nativeSimpleAlphaEffectTechset(m.techset) ||
                                   nativeCloudEffectTechset(m.techset) || simpleAdditive;
            if (materialType != 0x40200Cu || m.materialInfo[0x14] != 1 ||
                (oneBuffer ? m.materialInfo[0x15] != (simpleAdditive ? 2 : 1) ||
                                  m.materialInfo[0x16] != 1 ||
                                  m.materialInfo[0x1A] != 1 || m.materialInfo[0x1B] != 1
                           : (m.materialInfo[0x15] != 3 && m.materialInfo[0x15] != 4) ||
                                 m.materialInfo[0x16] != 2 || !m.materialInfo[0x1A] ||
                                 !m.materialInfo[0x1B]))
                throw std::runtime_error("Replay effect material alias has invalid metadata");
        }
        if (ownedDecalCarrier)
        {
            uint32_t surfaceFlags{}, materialType{};
            if (m.materialInfo.size() != 32)
                throw std::runtime_error("Replay decal carrier metadata is incomplete");
            std::memcpy(&surfaceFlags, m.materialInfo.data() + 4, sizeof(surfaceFlags));
            std::memcpy(&materialType, m.materialInfo.data() + 0xC, sizeof(materialType));
            if (surfaceFlags != 98304u || materialType != 0x800000u ||
                m.materialInfo[0x10] != 3 || m.materialInfo[0x11] != 29 ||
                m.materialInfo[0x14] || m.materialInfo[0x15] || m.materialInfo[0x16] ||
                m.materialInfo[0x17] || m.materialInfo[0x18] || m.materialInfo[0x19] ||
                m.materialInfo[0x1A] != 1 || m.materialInfo[0x1B] != 1 ||
                !m.constants.empty() || !m.bufferIndices.empty())
                throw std::runtime_error("Replay decal carrier has invalid native metadata");
        }
        if (m.materialInfo.size() != 32 ||
            (!ownedDecalCarrier && m.bufferIndices.size() != 195) ||
            (!m.techset.starts_with("w/lit_3_") && m.techset != "tw/mw120r_graybox_v1" &&
             !(owned && m.techset.starts_with("tw/mw120r_mp_") && !m.techniques.empty()) &&
             !(ownedEffect && m.techset.starts_with("tw/mw120r_fx_") && !m.techniques.empty()) &&
             !nativeEffectAlias && !ownedDecalCarrier) ||
            m.techset.size() > 200)
            throw std::runtime_error("Invalid Replay material metadata");
        if (d.contains("imageDefinitions"))
            for (const auto &im : d.at("imageDefinitions"))
            {
                Image image = LoadImageDefinition(path, im, 0);
                if (std::any_of(m.imageDefinitions.begin(), m.imageDefinitions.end(),
                                [&](const Image &x) { return x.name == image.name; }))
                    throw std::runtime_error("Duplicate Replay image definition");
                m.imageDefinitions.push_back(std::move(image));
            }
        if (hasOwnedDecalDefinition)
        {
            const auto &volume = m.decalVolumeDefinition;
            if (!ownedDecalCarrier || volume.flags != 1100u || !volume.rows ||
                !volume.columns ||
                std::any_of(volume.colorTint.begin(), volume.colorTint.end(),
                            [](const float value) { return !std::isfinite(value); }))
                throw std::runtime_error("Replay decal volume definition is incompatible");
            unsigned channelCount = 0;
            for (const auto &channel : volume.channels)
                if (!channel.empty())
                {
                    ++channelCount;
                    if (std::none_of(m.imageDefinitions.begin(), m.imageDefinitions.end(),
                                     [&](const Image &image) { return image.name == channel; }))
                        throw std::runtime_error(
                            "Replay decal volume channel has no image definition");
                }
            if (channelCount < 2 || volume.channels[0].empty() || volume.channels[2].empty())
                throw std::runtime_error("Replay decal volume image closure is incomplete");
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
        if (!m.techniques.empty() &&
            !nativeReplayFixture &&
            (m.techset == "tw/mw120r_graybox_v1" || m.techset.starts_with("tw/mw120r_mp_")))
        {
            uint32_t materialType{};
            std::memcpy(&materialType, m.materialInfo.data() + 0xC, sizeof(materialType));
            validateGeneratedTextureBindings(m, materialType == 589844u);
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
        if (nativeEffectAlias)
        {
            const bool oneBuffer = nativeSimpleAlphaEffectTechset(m.techset) ||
                                   nativeCloudEffectTechset(m.techset) ||
                                   nativeSimpleAdditiveEffectTechset(m.techset);
            const bool common = m.images.size() == 1 && m.textureHeaders.size() == 8 &&
                                m.textureHeaders[0] == 18 &&
                                std::all_of(m.textureHeaders.begin() + 1,
                                            m.textureHeaders.end(),
                                            [](const uint8_t value) { return value == 0; });
            const bool oneBufferBindings =
                m.buffers.size() == 1 && m.buffers.front()[0].empty() &&
                m.buffers.front()[1].empty() && m.buffers.front()[2].empty() &&
                m.buffers.front()[3].size() == 48;
            const bool featherBuffers =
                m.buffers.size() == 2 && m.buffers.front()[0].size() == 160 &&
                m.buffers.front()[1].empty() && m.buffers.front()[2].empty() &&
                m.buffers.front()[3].empty() && m.buffers.back()[0].empty() &&
                m.buffers.back()[1].empty() && m.buffers.back()[2].empty() &&
                m.buffers.back()[3].empty();
            if (!common || (oneBuffer ? !oneBufferBindings : !featherBuffers))
                throw std::runtime_error("Replay effect material alias has invalid bindings");
        }
        if (ownedDecalCarrier &&
            (!m.images.empty() || !m.textureHeaders.empty() || !m.buffers.empty()))
            throw std::runtime_error("Replay decal carrier owns unexpected tables");
    }
    return m;
}

namespace
{
constexpr uint32_t kUmbraVersion = 0xD6000014;
constexpr uint32_t kUmbraHeaderSize = 0x170;
constexpr uint32_t kUmbraTileHeaderSize = 0x60;

uint32_t read32(const std::vector<uint8_t> &data, size_t offset)
{
    if (offset > data.size() || data.size() - offset < sizeof(uint32_t))
        throw std::runtime_error("Umbra read exceeds tome data");
    uint32_t value;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

float readFloat(const std::vector<uint8_t> &data, size_t offset)
{
    if (offset > data.size() || data.size() - offset < sizeof(float))
        throw std::runtime_error("Umbra float read exceeds tome data");
    float value;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

void align16(std::vector<uint8_t> &data)
{
    data.resize((data.size() + 15) & ~size_t(15));
}

uint32_t allocate16(std::vector<uint8_t> &data, size_t size)
{
    align16(data);
    if (data.size() > UINT32_MAX || size > UINT32_MAX - data.size())
        throw std::runtime_error("Umbra tome exceeds the Replay offset range");
    const auto offset = static_cast<uint32_t>(data.size());
    data.resize(data.size() + size);
    return offset;
}

uint32_t crc32c(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1u)));
    }
    return ~crc;
}

unsigned objectIndexWidth(uint32_t objectCount)
{
    unsigned width = 1;
    while (width < 31 && (uint32_t(1) << width) < objectCount)
        ++width;
    return width;
}

std::vector<uint8_t> packObjectIndices(uint32_t objectCount, unsigned width)
{
    const uint64_t bitCount = uint64_t(objectCount) * width;
    if (bitCount > UINT32_MAX * uint64_t(8))
        throw std::runtime_error("Umbra object list exceeds the Replay bitstream limit");
    std::vector<uint8_t> result(size_t((bitCount + 31) / 32 * 4));
    uint64_t bitOffset = 0;
    for (uint32_t object = 0; object < objectCount; ++object)
    {
        for (unsigned bit = 0; bit < width; ++bit)
            if (object & (uint32_t(1) << bit))
                result[size_t(bitOffset + bit) >> 3] |= uint8_t(1u << ((bitOffset + bit) & 7));
        bitOffset += width;
    }
    return result;
}

uint32_t unpackObjectIndex(const std::vector<uint8_t> &data, uint32_t offset, uint64_t bitOffset,
                           unsigned width)
{
    uint32_t result = 0;
    for (unsigned bit = 0; bit < width; ++bit)
    {
        const auto absolute = bitOffset + bit;
        const auto byte = uint64_t(offset) + (absolute >> 3);
        if (byte >= data.size())
            throw std::runtime_error("Umbra object list is truncated");
        result |= uint32_t((data[size_t(byte)] >> (absolute & 7)) & 1u) << bit;
    }
    return result;
}

void validateRange(const std::vector<uint8_t> &data, uint32_t offset, uint64_t size,
                   const char *name)
{
    if (!offset || (offset & 15) || uint64_t(offset) + size > data.size())
        throw std::runtime_error(std::string("Invalid Umbra ") + name + " range");
}

void validateUmbraTome(const std::vector<uint8_t> &data, uint32_t objectCount,
                       uint32_t worldSurfaceCount, unsigned width)
{
    if (data.size() < kUmbraHeaderSize || data.size() > UINT32_MAX ||
        read32(data, 0) != kUmbraVersion || read32(data, 8) != data.size())
        throw std::runtime_error("Invalid conservative Umbra header");
    if (read32(data, 4) != crc32c(data.data() + 8, data.size() - 8))
        throw std::runtime_error("Invalid conservative Umbra checksum");
    if (read32(data, 0x2C) != 33 || read32(data, 0x38) != 0 || read32(data, 0x40) != objectCount ||
        read32(data, 0x4C) != 0 || read32(data, 0x54) != width ||
        read32(data, 0x5C) != objectCount || read32(data, 0x7C) != 0 || read32(data, 0x8C) != 1 ||
        read32(data, 0x90) != 1 || read32(data, 0x94) != 0)
        throw std::runtime_error("Invalid conservative Umbra topology");

    validateRange(data, read32(data, 0x30), 4, "top-level tree");
    validateRange(data, read32(data, 0x34), 4, "top-level map");
    validateRange(data, read32(data, 0x44), uint64_t(objectCount) * 24, "object bounds");
    validateRange(data, read32(data, 0x48), uint64_t(objectCount) * 32, "object distances");
    validateRange(data, read32(data, 0x50), uint64_t(objectCount) * 4, "user IDs");
    validateRange(data, read32(data, 0x58), (uint64_t(objectCount) * width + 31) / 32 * 4,
                  "object list");
    validateRange(data, read32(data, 0x88), 8, "cell starts");
    validateRange(data, read32(data, 0x9C), 4, "tile LOD");
    validateRange(data, read32(data, 0xA0), 4, "tile table");
    validateRange(data, read32(data, 0x14C), 4, "tile portal expansion");
    if (read32(data, read32(data, 0x30)) != 3 || read32(data, read32(data, 0x34)) != 0 ||
        read32(data, read32(data, 0x88)) != 0 || read32(data, read32(data, 0x88) + 4) != 1 ||
        readFloat(data, read32(data, 0x9C)) != 1.0f || readFloat(data, read32(data, 0x14C)) != 0.0f)
        throw std::runtime_error("Invalid conservative Umbra tile metadata");

    const uint32_t userIDs = read32(data, 0x50);
    const uint32_t objectLists = read32(data, 0x58);
    for (uint32_t object = 0; object < objectCount; ++object)
    {
        const uint32_t expectedUserID =
            object < worldSurfaceCount ? object : 0x10000000u + object - worldSurfaceCount;
        if (read32(data, userIDs + object * 4) != expectedUserID ||
            unpackObjectIndex(data, objectLists, uint64_t(object) * width, width) != object)
            throw std::runtime_error("Conservative Umbra object mapping is not lossless");
    }

    const uint32_t tile = read32(data, read32(data, 0xA0));
    validateRange(data, tile, kUmbraTileHeaderSize, "tile");
    const uint32_t sizeAndFlags = read32(data, tile + 0x2C);
    const uint32_t tileSize = sizeAndFlags >> 8;
    if ((sizeAndFlags & 0xFF) != 3 || tileSize < kUmbraTileHeaderSize ||
        uint64_t(tile) + tileSize > data.size() || read32(data, tile + 0x18) != 33 ||
        read32(data, tile + 0x34) != 1)
        throw std::runtime_error("Invalid conservative Umbra tile header");
    validateRange(data, tile + read32(data, tile + 0x1C), 4, "tile tree");
    validateRange(data, tile + read32(data, tile + 0x20), 4, "tile map");
    validateRange(data, tile + read32(data, tile + 0x38), 36, "tile cell");
    if (read32(data, tile + read32(data, tile + 0x1C)) != 3 ||
        read32(data, tile + read32(data, tile + 0x20)) != 0)
        throw std::runtime_error("Invalid conservative Umbra tile traversal");
    const uint32_t cell = tile + read32(data, tile + 0x38);
    if (read32(data, cell) != 0 || read32(data, cell + 4) != 0 || read32(data, cell + 8) != 0 ||
        read32(data, cell + 12) != objectCount || read32(data, cell + 16) != UINT32_MAX ||
        read32(data, cell + 20) != 0x80000000 || read32(data, cell + 24) != 0 ||
        read32(data, cell + 28) != 0x0000FFFF || read32(data, cell + 32) != UINT32_MAX)
        throw std::runtime_error("Invalid conservative Umbra cell");
}
} // namespace

replaybounds::Bounds LoadBounds(const nlohmann::json &value)
{
    if (!value.is_array() || value.size() != 2 || !value[0].is_array() || !value[1].is_array() ||
        value[0].size() != 3 || value[1].size() != 3)
        throw std::runtime_error("Brush-model bounds require two three-component vectors");

    replaybounds::Bounds bounds;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const double minimum = value[0][axis].get<double>();
        const double maximum = value[1][axis].get<double>();
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum ||
            std::abs(minimum) > 100000 || std::abs(maximum) > 100000)
            throw std::runtime_error("Invalid brush-model bounds");
        bounds.midpoint[axis] = float((minimum + maximum) * 0.5);
        const double required =
            std::max(maximum - bounds.midpoint[axis], bounds.midpoint[axis] - minimum);
        bounds.halfSize[axis] = float(required);
        if (double(bounds.halfSize[axis]) < required)
            bounds.halfSize[axis] =
                std::nextafter(bounds.halfSize[axis], std::numeric_limits<float>::infinity());
    }
    return bounds;
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
    {
        if (!j.at("additionalMaterials").is_array() ||
            j.at("additionalMaterials").size() > 4096)
            throw std::runtime_error("Invalid surface material table");
        for (const auto &definition : j.at("additionalMaterials"))
            m.additionalMaterials.push_back(LoadMaterial(path, definition));
    }
    if (j.contains("assetMaterials"))
    {
        const auto &definitions = j.at("assetMaterials");
        if (!definitions.is_array() || definitions.size() > 4096)
            throw std::runtime_error("Invalid auxiliary material table");
        for (const auto &definition : definitions)
            m.assetMaterials.push_back(LoadMaterial(path, definition, true));
    }
    const auto &list = j.at("surfaces");
    const unsigned atlasLayout = j.value("atlasVertexLayout", 1u);
    if (atlasLayout < 1 || atlasLayout > 3)
        throw std::runtime_error("Unsupported atlas vertex layout");
    if (!list.is_array() || list.empty() || list.size() > 4096)
        throw std::runtime_error("Invalid surface count");
    m.count = unsigned(list.size());
    if (j.contains("brushModels"))
    {
        const auto &models = j.at("brushModels");
        if (!models.is_array() || models.empty() || models.size() > 65535)
            throw std::runtime_error("Invalid brush-model table");
        unsigned nextSurface = 0;
        for (const auto &source : models)
        {
            BrushModel model;
            model.firstSurface = source.at("firstSurface").get<unsigned>();
            model.surfaceCount = source.at("surfaceCount").get<unsigned>();
            model.bounds = LoadBounds(source.at("bounds"));
            if (model.firstSurface != nextSurface || model.surfaceCount > m.count - nextSurface)
                throw std::runtime_error("Brush-model surface ranges must be contiguous");
            nextSurface += model.surfaceCount;
            m.brushModels.push_back(model);
        }
        if (nextSurface != m.count)
            throw std::runtime_error("Brush-model table does not cover every surface");
    }
    else
    {
        m.brushModels.push_back({{}, 0, m.count});
    }
    const auto &dpvs = j.at("dpvs");
    const auto &planes = dpvs.at("planes");
    const auto &nodes = dpvs.at("nodes");
    const auto &cells = dpvs.at("cells");
    if (!planes.is_array() || planes.size() > 65535 || !nodes.is_array() || nodes.empty() ||
        nodes.size() > 65535 || !cells.is_array() || cells.empty() || cells.size() > 65535)
        throw std::runtime_error("Invalid Replay DPVS topology");
    const auto vector = [](const nlohmann::json &source, const std::size_t count) {
        if (!source.is_array() || source.size() != count)
            throw std::runtime_error("Invalid Replay DPVS vector");
        std::vector<float> result(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            result[index] = source.at(index).get<float>();
            if (!std::isfinite(result[index]))
                throw std::runtime_error("Non-finite Replay DPVS vector");
        }
        return result;
    };
    if (j.contains("glassPanes"))
    {
        const auto &panes = j.at("glassPanes");
        if (!panes.is_array() || panes.size() > 65535)
            throw std::runtime_error("Invalid Replay glass-pane table");
        for (const auto &source : panes)
        {
            GlassPane pane;
            pane.material = source.at("material").get<std::string>();
            pane.materialShattered = source.value("materialShattered", pane.material);
            for (const std::string &name : {pane.material, pane.materialShattered})
            {
                const auto material = std::find_if(
                    m.assetMaterials.begin(), m.assetMaterials.end(),
                    [&](const Material &candidate) { return candidate.material == name; });
                if (material == m.assetMaterials.end() || material->techniques.empty() ||
                    std::any_of(material->techniques.begin(), material->techniques.end(),
                                [](const Technique &technique) {
                                    return technique.header[0x9C] != 31;
                                }))
                    throw std::runtime_error("Glass pane requires native glass materials");
            }
            const auto origin = vector(source.at("origin"), 3);
            const auto quaternion = vector(source.at("quaternion"), 4);
            const auto texVecs = vector(source.at("texVecs"), 4);
            const auto texOrigin = vector(source.at("texCoordOrigin"), 2);
            std::copy(texVecs.begin(), texVecs.end(), pane.texVecs.begin());
            std::copy(texOrigin.begin(), texOrigin.end(), pane.texCoordOrigin.begin());
            std::copy(origin.begin(), origin.end(), pane.origin.begin());
            std::copy(quaternion.begin(), quaternion.end(), pane.quaternion.begin());
            pane.halfWidth = source.at("halfWidth").get<float>();
            pane.halfHeight = source.at("halfHeight").get<float>();
            pane.halfThickness = source.at("halfThickness").get<float>();
            const float quaternionLength = std::inner_product(
                pane.quaternion.begin(), pane.quaternion.end(), pane.quaternion.begin(), 0.0f);
            if (std::abs(quaternionLength - 1.0f) > 0.001f || pane.halfWidth < 0.125f ||
                pane.halfHeight < 0.125f || pane.halfThickness < 0.125f ||
                pane.halfWidth * 32.0f > 32767.0f || pane.halfHeight * 32.0f > 32767.0f)
                throw std::runtime_error("Invalid Replay glass-pane geometry");
            m.glassPanes.push_back(pane);
        }
    }
    const auto &reflectionProbes = j.at("reflectionProbes");
    if (!reflectionProbes.is_array() || reflectionProbes.empty() || reflectionProbes.size() > 256)
        throw std::runtime_error("Invalid Replay reflection-probe table");
    std::set<std::string> reflectionImages;
    for (const auto &source : reflectionProbes)
    {
        ReflectionProbe probe;
        const auto origin = vector(source.at("origin"), 3);
        std::copy(origin.begin(), origin.end(), probe.origin.begin());
        probe.volume = LoadBounds(source.at("volume"));
        probe.image = LoadImageDefinition(path, source.at("image"), 0x8000u);
        if (!reflectionImages.insert(probe.image.name).second)
            throw std::runtime_error("Duplicate Replay reflection-probe image");
        const auto &sh = source.at("sh");
        if (!sh.is_array() || sh.size() != probe.sh.size())
            throw std::runtime_error("Replay reflection probe requires four SH vectors");
        for (std::size_t channel = 0; channel < probe.sh.size(); ++channel)
        {
            if (!sh.at(channel).is_array() || sh.at(channel).size() != probe.sh[channel].size())
                throw std::runtime_error("Replay reflection-probe SH vector has invalid width");
            for (std::size_t coefficient = 0; coefficient < probe.sh[channel].size(); ++coefficient)
            {
                probe.sh[channel][coefficient] = sh.at(channel).at(coefficient).get<float>();
                if (!std::isfinite(probe.sh[channel][coefficient]))
                    throw std::runtime_error("Replay reflection-probe SH value is non-finite");
            }
        }
        m.reflectionProbes.push_back(std::move(probe));
    }
    m.reflectionProbeArrayImage =
        LoadImageDefinition(path, j.at("reflectionProbeArrayImage"), 0x20000u);
    if (m.reflectionProbeArrayImage.numElements != m.reflectionProbes.size())
        throw std::runtime_error("Replay reflection array does not match probe count");
    if (j.contains("nativeLightmaps"))
    {
        const auto &lightmaps = j.at("nativeLightmaps");
        // Conversion preserves a single source lightmap's dimensions, or
        // packs multiple source pairs into one native 4096x4096 atlas.
        if (!lightmaps.is_array() || lightmaps.size() > 1)
            throw std::runtime_error("Invalid native Replay lightmap table");
        constexpr std::array<uint32_t, 3> expectedFormats{39u, 32u, 40u};
        for (const auto &source : lightmaps)
        {
            const unsigned width = source.at("width").get<unsigned>();
            const unsigned height = source.at("height").get<unsigned>();
            const auto &images = source.at("images");
            if (!width || !height || width > UINT16_MAX || height > UINT16_MAX ||
                !images.is_array() || images.size() != expectedFormats.size())
                throw std::runtime_error("Invalid native Replay lightmap dimensions");
            NativeLightmap lightmap;
            for (std::size_t channel = 0; channel < expectedFormats.size(); ++channel)
            {
                const auto &imageSource = images.at(channel);
                const unsigned format = imageSource.at("format").get<unsigned>();
                const std::string file = imageSource.at("pixels").get<std::string>();
                if (format != expectedFormats[channel] ||
                    std::filesystem::path(file).filename() != file ||
                    file.find("..") != std::string::npos)
                    throw std::runtime_error("Invalid native Replay lightmap image");
                const std::size_t blocks = static_cast<std::size_t>((width + 3) / 4) *
                                           ((height + 3) / 4);
                const std::size_t expectedSize =
                    channel == 0 ? blocks * 8
                                 : channel == 1 ? static_cast<std::size_t>(width) * height * 4
                                                : blocks * 16;
                const auto pixelPath = std::filesystem::path(path).parent_path() / file;
                std::error_code error;
                if (expectedSize > UINT32_MAX ||
                    !std::filesystem::is_regular_file(pixelPath, error) ||
                    std::filesystem::file_size(pixelPath, error) != expectedSize)
                    throw std::runtime_error("Native Replay lightmap image length mismatch");
                auto &image = lightmap.images[channel];
                image.format = format;
                image.width = static_cast<uint16_t>(width);
                image.height = static_cast<uint16_t>(height);
                image.pixels.resize(expectedSize);
                std::ifstream input(pixelPath, std::ios::binary);
                if (!input.read(reinterpret_cast<char *>(image.pixels.data()), expectedSize))
                    throw std::runtime_error("Cannot read native Replay lightmap pixels");
            }
            m.nativeLightmaps.push_back(std::move(lightmap));
        }
    }
    for (const auto &source : planes)
    {
        const auto normal = vector(source.at("normal"), 3);
        const float distance = source.at("dist").get<float>();
        const unsigned type = source.at("type").get<unsigned>();
        if (!std::isfinite(distance) || type > 3)
            throw std::runtime_error("Invalid Replay DPVS plane");
        m.planes.push_back(
            {{normal[0], normal[1], normal[2]}, distance, static_cast<uint8_t>(type)});
    }
    for (const auto &source : nodes)
    {
        const auto node = source.get<unsigned>();
        if (node > 65535)
            throw std::runtime_error("Replay DPVS node exceeds its native width");
        m.nodes.push_back(static_cast<uint16_t>(node));
    }
    for (const auto &source : cells)
    {
        Cell cell;
        cell.bounds = LoadBounds(source.at("bounds"));
        for (const auto &item : source.at("portals"))
        {
            Portal portal;
            const auto plane = vector(item.at("plane"), 4);
            std::copy(plane.begin(), plane.end(), portal.plane.begin());
            portal.cell = item.at("cell").get<unsigned>();
            if (portal.cell >= cells.size())
                throw std::runtime_error("Replay DPVS portal references an invalid cell");
            const auto &vertices = item.at("vertices");
            if (!vertices.is_array() || vertices.size() < 3 || vertices.size() > 255)
                throw std::runtime_error("Replay DPVS portal has an invalid vertex count");
            for (const auto &itemVertex : vertices)
            {
                const auto value = vector(itemVertex, 3);
                portal.vertices.push_back({value[0], value[1], value[2]});
            }
            const auto &axes = item.at("hull_axis");
            if (!axes.is_array() || axes.size() != 2)
                throw std::runtime_error("Replay DPVS portal has invalid hull axes");
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                const auto value = vector(axes.at(axis), 3);
                portal.hullAxis[axis] = {value[0], value[1], value[2]};
            }
            cell.portals.push_back(std::move(portal));
        }
        std::vector<CellTree> leaves;
        const auto &trees = source.at("trees");
        if (!trees.is_array())
            throw std::runtime_error("Replay DPVS cell has invalid AABB trees");
        if (!trees.empty() && trees.at(0).contains("childCount"))
        {
            for (std::size_t treeIndex = 0; treeIndex < trees.size(); ++treeIndex)
            {
                const auto &tree = trees.at(treeIndex);
                const auto childCount = tree.at("childCount").get<unsigned>();
                const auto firstChild = tree.at("firstChild").get<unsigned>();
                if (childCount > UINT16_MAX ||
                    (childCount && (firstChild <= treeIndex ||
                                    firstChild + childCount > trees.size())) ||
                    (!childCount && firstChild))
                    throw std::runtime_error("Replay DPVS AABB child range is invalid");
                const auto bounds = LoadBounds(tree.at("bounds"));
                auto owned = tree.at("surfaces").get<std::vector<unsigned>>();
                std::ranges::sort(owned);
                owned.erase(std::unique(owned.begin(), owned.end()), owned.end());
                for (const auto surface : owned)
                    if (surface >= m.worldSurfaceCount())
                        throw std::runtime_error("Replay DPVS cell references an invalid surface");
                const unsigned firstSurface = owned.empty() ? 0 : owned.front();
                const unsigned surfaceCount = owned.empty() ? 0 : owned.back() - firstSurface + 1;
                const auto models = tree.at("models").get<std::vector<unsigned>>();
                std::vector<uint16_t> ownedModels;
                ownedModels.reserve(models.size());
                for (const auto model : models)
                {
                    if (model > UINT16_MAX)
                        throw std::runtime_error("Replay DPVS static-model index exceeds its native width");
                    ownedModels.push_back(static_cast<uint16_t>(model));
                }
                cell.trees.push_back({bounds, firstSurface, surfaceCount,
                                      childCount ? static_cast<unsigned>((firstChild - treeIndex) * 48) : 0,
                                      static_cast<uint16_t>(childCount), std::move(ownedModels)});
            }
            m.cells.push_back(std::move(cell));
            continue;
        }
        for (const auto &tree : trees)
        {
            const auto bounds = LoadBounds(tree.at("bounds"));
            std::vector<unsigned> owned = tree.at("surfaces").get<std::vector<unsigned>>();
            const auto models = tree.value("models", nlohmann::json::array());
            if (!models.is_array())
                throw std::runtime_error("Replay DPVS AABB tree has invalid static models");
            std::vector<uint16_t> ownedModels;
            ownedModels.reserve(models.size());
            for (const auto &model : models)
            {
                const auto index = model.get<unsigned>();
                if (index > UINT16_MAX)
                    throw std::runtime_error(
                        "Replay DPVS static-model index exceeds its native width");
                ownedModels.push_back(static_cast<uint16_t>(index));
            }
            std::ranges::sort(owned);
            owned.erase(std::unique(owned.begin(), owned.end()), owned.end());
            bool firstLeaf = true;
            for (std::size_t begin = 0; begin < owned.size();)
            {
                if (owned[begin] >= m.worldSurfaceCount())
                    throw std::runtime_error("Replay DPVS cell references an invalid surface");
                std::size_t end = begin + 1;
                while (end < owned.size() && owned[end] == owned[end - 1] + 1)
                    ++end;
                std::vector<uint16_t> leafModels;
                if (firstLeaf)
                    leafModels = std::move(ownedModels);
                leaves.push_back({bounds, owned[begin], static_cast<unsigned>(end - begin), 0, 0,
                                  std::move(leafModels)});
                firstLeaf = false;
                begin = end;
            }
            if (firstLeaf && !ownedModels.empty())
                leaves.push_back({bounds, 0, 0, 0, 0, std::move(ownedModels)});
        }
        if (leaves.size() > 65535)
            throw std::runtime_error("Replay DPVS cell exceeds the native AABB child limit");
        if (leaves.size() <= 1)
        {
            cell.trees = std::move(leaves);
        }
        else
        {
            // Replay's native multi-child AABB trees retain the aggregate
            // world-surface range and static-model references on the parent.
            // The children refine that set; an empty parent can lose objects
            // when the renderer selects the coarse visibility path.
            unsigned firstSurface = m.worldSurfaceCount();
            unsigned lastSurface = 0;
            std::vector<uint16_t> parentModels;
            for (const CellTree &leaf : leaves)
            {
                if (leaf.surfaceCount)
                {
                    firstSurface = std::min(firstSurface, leaf.firstSurface);
                    lastSurface = std::max(lastSurface, leaf.firstSurface + leaf.surfaceCount);
                }
                parentModels.insert(parentModels.end(), leaf.staticModelIndexes.begin(),
                                    leaf.staticModelIndexes.end());
            }
            std::ranges::sort(parentModels);
            parentModels.erase(std::unique(parentModels.begin(), parentModels.end()),
                               parentModels.end());
            if (parentModels.size() > UINT16_MAX)
                throw std::runtime_error("Replay AABB parent has too many static models");
            if (firstSurface == m.worldSurfaceCount())
                firstSurface = 0;
            cell.trees.push_back({cell.bounds, firstSurface, lastSurface - firstSurface,
                                  48, static_cast<uint16_t>(leaves.size()),
                                  std::move(parentModels)});
            cell.trees.insert(cell.trees.end(), leaves.begin(), leaves.end());
        }
        m.cells.push_back(std::move(cell));
    }
    m.surfaces.resize(m.count * 40);
    m.bounds.resize(m.count * 56);
    m.drawSurfs.resize(m.count * 16);
    m.surfData.resize(m.count * 88);
    // Native buffers reserve offset zero. Stock first surface starts at byte four.
    m.positions.resize(4);
    m.aux.resize(4);
    replaybounds::Accumulator sceneBounds, drawBounds;
    std::size_t activeModel = 0;
    unsigned opaqueInModel = 0;
    for (unsigned i = 0; i < m.count; ++i)
    {
        while (activeModel + 1 < m.brushModels.size() &&
               i >= m.brushModels[activeModel].firstSurface +
                        m.brushModels[activeModel].surfaceCount)
        {
            ++activeModel;
            opaqueInModel = 0;
        }
        const auto &s = list[i];
        const auto &vertices = s.at("vertices");
        const auto &indices = s.at("indices");
        const unsigned material = s.value("materialIndex", 0u);
        const unsigned surfaceLayout = s.value("atlasVertexLayout", atlasLayout);
        const unsigned lightmapIndex = s.value("lightmapIndex", 0u);
        if (material > m.additionalMaterials.size())
            throw std::runtime_error("Invalid surface material index");
        if (surfaceLayout < 1 || surfaceLayout > 3)
            throw std::runtime_error("Unsupported surface vertex layout");
        if (lightmapIndex > 511)
            throw std::runtime_error("Invalid Replay surface lightmap index");
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
        const bool opaque = s.value("opaque", false) || !material || sky || maskedPrepass;
        const unsigned modelRelativeSurface = i - m.brushModels[activeModel].firstSurface;
        if (opaque && opaqueInModel != modelRelativeSurface)
            throw std::runtime_error("Opaque surfaces must precede transparent surfaces");
        if (opaque)
        {
            ++opaqueInModel;
            if (activeModel == 0)
                ++m.opaqueCount;
        }
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
            if (activeModel == 0)
                drawBounds.Add(point);
            if (!sky && activeModel == 0)
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
            if (surfaceLayout >= 2)
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
                if (surfaceLayout == 3)
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
                if (surfaceLayout >= 2)
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
        put(sf, 30, uint16_t(lightmapIndex));
        // Shipped sky surfaces keep the generic sun-shadow bit but are not
        // Umbra occluders. The synthetic sky cube must not hide map geometry.
        constexpr uint8_t castsSunShadow = 0x01;
        constexpr uint8_t umbraOccluder = 0x40;
        const unsigned sunShadowMask = s.value("sunShadowMask", 0u);
        if (sunShadowMask & ~0x3Eu)
            throw std::runtime_error("Surface sun-shadow mask exceeds Replay flags");
        put(sf, 32, uint8_t(castsSunShadow | sunShadowMask |
                            (sky ? 0 : umbraOccluder)));
        auto *bounds = m.bounds.data() + 56 * i;
        for (unsigned k = 0; k < 3; ++k)
        {
            put(bounds, 4 * k, (mins[k] + maxs[k]) * 0.5f);
            put(bounds, 12 + 4 * k, (maxs[k] - mins[k]) * 0.5f + 1.0f);
        }
        auto *gpu = m.surfData.data() + 88 * i;
        put(gpu, 4, uint32_t(surfaceLayout == 3 ? 4 : surfaceLayout));
        put(gpu, 8, posOffset);
        put(gpu, 12, normalOffset);
        put(gpu, 16, lmOffset);
        put(gpu, 20, colorOffset);
        put(gpu, 24, uvOffset);
    }
    if (j.contains("brushModels"))
        m.sceneBounds = m.brushModels.front().bounds;
    else
    {
        m.sceneBounds = sceneBounds.Finish();
        m.brushModels.front().bounds = m.sceneBounds;
    }
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
        put(w.data(), off, m.worldSurfaceCount());
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

namespace
{
constexpr size_t kStaticModelsOffset = 0x150;

struct StaticModelLayout
{
    struct Surface
    {
        uint16_t model{};
        uint8_t lod{};
        uint8_t index{};
        uint16_t material{};
        replaybounds::Bounds bounds{};
        float drawDistance{};
    };

    std::vector<Surface> surfaces;
    std::vector<uint16_t> firstSurface;
    std::vector<std::string> materials;
};

void ValidateBounds(const replaybounds::Bounds &bounds, const char *kind)
{
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(bounds.midpoint[axis]) || !std::isfinite(bounds.halfSize[axis]) ||
            bounds.halfSize[axis] < 0)
            throw std::runtime_error(std::string("Invalid static-model ") + kind + " bounds");
    }
}

StaticModelLayout BuildStaticModelLayout(const StaticModels &source)
{
    if (source.models.size() > UINT16_MAX || source.instances.size() > UINT16_MAX)
        throw std::runtime_error("Static-model table exceeds Replay counts");

    StaticModelLayout result;
    result.firstSurface.reserve(source.models.size());
    for (size_t modelIndex = 0; modelIndex < source.models.size(); ++modelIndex)
    {
        const auto &model = source.models[modelIndex];
        if (model.name.empty() || model.lods.empty() || model.lods.size() > 6)
            throw std::runtime_error("Invalid Replay static-model definition");
        ValidateBounds(model.bounds, "model");
        if (result.surfaces.size() > UINT16_MAX)
            throw std::runtime_error("Static-model surface table exceeds Replay indices");
        result.firstSurface.push_back(static_cast<uint16_t>(result.surfaces.size()));

        size_t modelSurfaceCount = 0;
        float previousDistance = 0;
        for (size_t lodIndex = 0; lodIndex < model.lods.size(); ++lodIndex)
        {
            const auto &lod = model.lods[lodIndex];
            if (lod.surfaces.empty() || lod.surfaces.size() > UINT8_MAX ||
                !std::isfinite(lod.distance) || lod.distance <= previousDistance)
                throw std::runtime_error("Invalid Replay static-model LOD");
            previousDistance = lod.distance;
            if (modelSurfaceCount + lod.surfaces.size() > UINT8_MAX)
                throw std::runtime_error("Static model exceeds Replay's per-model surface limit");

            for (size_t surfaceIndex = 0; surfaceIndex < lod.surfaces.size(); ++surfaceIndex)
            {
                const auto &surface = lod.surfaces[surfaceIndex];
                if (surface.material.empty())
                    throw std::runtime_error("Static-model surface has no material");
                ValidateBounds(surface.bounds, "surface");
                auto found =
                    std::find(result.materials.begin(), result.materials.end(), surface.material);
                if (found == result.materials.end())
                {
                    if (result.materials.size() == UINT16_MAX)
                        throw std::runtime_error(
                            "Static-model material table exceeds Replay indices");
                    result.materials.push_back(surface.material);
                    found = std::prev(result.materials.end());
                }
                result.surfaces.push_back({static_cast<uint16_t>(modelIndex),
                                           static_cast<uint8_t>(lodIndex),
                                           static_cast<uint8_t>(surfaceIndex),
                                           static_cast<uint16_t>(found - result.materials.begin()),
                                           surface.bounds, model.lods.back().distance});
            }
            modelSurfaceCount += lod.surfaces.size();
        }
    }
    if (result.surfaces.size() > UINT16_MAX)
        throw std::runtime_error("Static-model surface table exceeds Replay indices");
    for (const auto &instance : source.instances)
    {
        if (instance.model >= source.models.size() || !std::isfinite(instance.scale) ||
            instance.scale <= 0)
            throw std::runtime_error("Invalid Replay static-model instance");
        double quaternionLength = 0;
        for (const float value : instance.origin)
            if (!std::isfinite(value) || std::abs(double(value) * 4096.0) > INT32_MAX)
                throw std::runtime_error("Static-model origin exceeds Replay fixed-point range");
        for (const float value : instance.quaternion)
        {
            if (!std::isfinite(value))
                throw std::runtime_error("Static-model quaternion is not finite");
            quaternionLength += double(value) * value;
        }
        if (quaternionLength < 1.0e-12)
            throw std::runtime_error("Static-model quaternion has zero length");
    }
    return result;
}

std::array<float, 4> NormalizeQuaternion(const std::array<float, 4> &source)
{
    double length = 0;
    for (const float value : source)
        length += double(value) * value;
    const float inverse = static_cast<float>(1.0 / std::sqrt(length));
    std::array<float, 4> result{};
    for (unsigned index = 0; index < 4; ++index)
        result[index] = source[index] * inverse;
    return result;
}

replaybounds::Bounds TransformBounds(const replaybounds::Bounds &bounds,
                                     const StaticModelInstance &instance)
{
    const auto q = NormalizeQuaternion(instance.quaternion);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float rotation[3][3] = {
        {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
        {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
        {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    replaybounds::Bounds result;
    for (unsigned row = 0; row < 3; ++row)
    {
        result.midpoint[row] = instance.origin[row];
        result.halfSize[row] = 0;
        for (unsigned column = 0; column < 3; ++column)
        {
            result.midpoint[row] +=
                rotation[row][column] * bounds.midpoint[column] * instance.scale;
            result.halfSize[row] +=
                std::abs(rotation[row][column]) * bounds.halfSize[column] * instance.scale;
        }
    }
    return result;
}

uint16_t PackQuaternionComponent(const float value)
{
    return static_cast<uint16_t>(
        std::lround((std::clamp(value, -1.0f, 1.0f) * 0.5f + 0.5f) * 65535.0f));
}
} // namespace

void StampStaticModels(std::vector<uint8_t> &world, const StaticModels &models)
{
    const auto layout = BuildStaticModelLayout(models);
    if (models.models.empty())
    {
        if (!models.instances.empty())
            throw std::runtime_error("Static-model instances have no definitions");
        return;
    }
    if (world.size() < kStaticModelsOffset + 0x4F8)
        throw std::runtime_error("GfxWorld is too small for Replay static models");

    auto *staticWorld = world.data() + kStaticModelsOffset;
    put(staticWorld, 0x00, static_cast<uint32_t>(layout.surfaces.size()));
    put(staticWorld, 0x04, static_cast<uint32_t>(models.models.size()));
    put(staticWorld, 0x08, static_cast<uint32_t>(models.instances.size()));
    put(staticWorld, 0x10, static_cast<uint32_t>(models.instances.size()));
    put(staticWorld, 0x1C, static_cast<uint32_t>(layout.materials.size()));
    put(staticWorld, 0x38, PTR_FOLLOWS);  // GfxStaticModelSurface[]
    put(staticWorld, 0x40, PTR_FOLLOWS);  // GfxStaticModel[]
    put(staticWorld, 0x50, PTR_FOLLOWS);  // GfxStaticModelCollection[]
    put(staticWorld, 0x60, PTR_FOLLOWS);  // instanceFlags[]
    put(staticWorld, 0x68, PTR_FOLLOWS);  // collectionBounds[]
    put(staticWorld, 0x70, PTR_FOLLOWS);  // MaterialHandle[]
    put(staticWorld, 0x78, PTR_FOLLOWS);  // modelStaticIndirection[]
    put(staticWorld, 0x108, PTR_FOLLOWS); // smodelSurfData[]
    put(staticWorld, 0x150, PTR_FOLLOWS); // smodelInstanceData[]
    put(staticWorld, 0x288, PTR_FOLLOWS); // smodelExpansionData[]
    put(staticWorld, 0x2D0, PTR_FOLLOWS); // smodelSurfMatIndirection[]
    put(staticWorld, 0x398, PTR_FOLLOWS); // smodelSurfUGBIndirection[]

    auto *dpvs = world.data() + 0x3F98;
    const auto visibilityWordCount = static_cast<uint32_t>((models.instances.size() + 31) >> 5);
    put(dpvs, 0x00, visibilityWordCount);
    constexpr std::array<size_t, 24> serializedViews = {
        0, 1, 2, 3, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    for (const size_t index : serializedViews)
        put(dpvs, 0x18 + index * 8, PTR_FOLLOWS);
    put(dpvs, 0x258, PTR_FOLLOWS);
}

void EmitStaticModels(ZoneWriter &writer, const StaticModels &models,
                      unsigned lastSunPrimaryLightIndex)
{
    const auto layout = BuildStaticModelLayout(models);
    if (models.models.empty())
        return;

    writer.align(1);
    for (const auto &surface : layout.surfaces)
    {
        writer.writeT(surface.model);
        writer.writeT(surface.lod);
        writer.writeT(surface.index);
    }

    writer.align(7);
    for (size_t index = 0; index < models.models.size(); ++index)
    {
        uint8_t model[16]{};
        put(model, 0x00, writer.assetAlias(ASSET_TYPE_XMODEL, models.models[index].name));
        model[0x08] = 1; // STATIC_MODEL_FLAG_LIGHTPROBE_LIGHTING
        put(model, 0x0A, layout.firstSurface[index]);
        writer.write(model, sizeof(model));
    }

    // Replay selects bit min(activePrimarySunLight, 5) - 1, not a cascade.
    // Keep each collection eligible for every directional sun in this world.
    const auto sunShadowMask =
        static_cast<uint8_t>((1u << std::min(lastSunPrimaryLightIndex, 5u)) - 1u);
    writer.align(3);
    for (size_t index = 0; index < models.instances.size(); ++index)
    {
        const auto &instance = models.instances[index];
        uint8_t collection[16]{};
        put(collection, 0x00, static_cast<uint32_t>(index));
        put(collection, 0x04, uint32_t{1});
        put(collection, 0x08, static_cast<uint16_t>(instance.model));
        collection[0x0E] = sunShadowMask;
        writer.write(collection, sizeof(collection));
    }
    for (size_t index = 0; index < models.instances.size(); ++index)
        writer.writeT<uint8_t>(6);

    writer.align(3);
    for (const auto &instance : models.instances)
    {
        const auto bounds = TransformBounds(models.models[instance.model].bounds, instance);
        uint8_t value[24]{};
        bounds.Write(value);
        writer.write(value, sizeof(value));
    }

    writer.align(7);
    for (const auto &material : layout.materials)
        writer.writeT(writer.assetAlias(ASSET_TYPE_MATERIAL, material));

    writer.align(3);
    for (size_t index = 0; index < models.instances.size(); ++index)
    {
        writer.writeT(static_cast<uint32_t>(index));
        writer.writeT<uint16_t>(0);
        writer.writeT<uint16_t>(0);
    }
    writer.align(15);
    for (const auto &surface : layout.surfaces)
    {
        uint8_t data[32]{};
        std::memcpy(data, surface.bounds.midpoint.data(), 3 * sizeof(float));
        put(data, 0x0C,
            *std::max_element(surface.bounds.halfSize.begin(), surface.bounds.halfSize.end()));
        std::memcpy(data + 0x10, surface.bounds.halfSize.data(), 3 * sizeof(float));
        put(data, 0x1C, surface.drawDistance);
        writer.write(data, sizeof(data));
    }
    writer.align(15);
    for (const auto &instance : models.instances)
    {
        const auto quaternion = NormalizeQuaternion(instance.quaternion);
        uint8_t data[24]{};
        for (unsigned axis = 0; axis < 3; ++axis)
            put(data, axis * 4,
                static_cast<int32_t>(std::lround(double(instance.origin[axis]) * 4096.0)));
        for (unsigned component = 0; component < 4; ++component)
            put(data, 0x0C + component * 2, PackQuaternionComponent(quaternion[component]));
        put(data, 0x14, instance.scale);
        writer.write(data, sizeof(data));
    }
    writer.align(3);
    for (size_t modelIndex = 0; modelIndex < models.models.size(); ++modelIndex)
    {
        const auto &model = models.models[modelIndex];
        uint8_t expansion[64]{};
        const size_t first = layout.firstSurface[modelIndex];
        size_t surfaceOffset = 0;
        put(expansion, 0x00, static_cast<uint16_t>(first));
        expansion[0x02] = static_cast<uint8_t>(std::accumulate(
            model.lods.begin(), model.lods.end(), size_t{},
            [](size_t total, const StaticModelLod &lod) { return total + lod.surfaces.size(); }));
        expansion[0x03] = static_cast<uint8_t>(model.lods.size());
        model.bounds.Write(expansion + 0x04);
        for (size_t lodIndex = 0; lodIndex < model.lods.size(); ++lodIndex)
        {
            expansion[0x1C + lodIndex * 2] = static_cast<uint8_t>(surfaceOffset);
            expansion[0x1D + lodIndex * 2] =
                static_cast<uint8_t>(model.lods[lodIndex].surfaces.size());
            put(expansion, 0x28 + lodIndex * 4, model.lods[lodIndex].distance);
            surfaceOffset += model.lods[lodIndex].surfaces.size();
        }
        writer.write(expansion, sizeof(expansion));
    }
    writer.align(1);
    for (const auto &surface : layout.surfaces)
    {
        writer.writeT(surface.material);
        writer.writeT<uint8_t>(0xEF);
        writer.writeT<uint8_t>(0x0F);
    }
    writer.align(3);
    for (size_t index = 0; index < layout.surfaces.size(); ++index)
        writer.writeT<uint32_t>(0);
}

void StampReflectionProbes(std::vector<uint8_t> &world, const Mesh &mesh,
                           const ZoneWriter &writer)
{
    constexpr size_t drawOffset = 0x648;
    constexpr size_t dataSize = 0xD0;
    if (world.size() < drawOffset + dataSize || mesh.reflectionProbes.empty() ||
        mesh.reflectionProbes.size() > UINT16_MAX)
        throw std::runtime_error("Invalid Replay reflection-probe world");
    auto *data = world.data() + drawOffset;
    put(data, 0x00, static_cast<uint32_t>(mesh.reflectionProbes.size()));
    put(data, 0x18, PTR_FOLLOWS); // GfxReflectionProbe[]
    put(data, 0x28,
        writer.assetAlias(ASSET_TYPE_IMAGE, mesh.reflectionProbeArrayImage.name));
    put(data, 0x58, static_cast<uint32_t>(mesh.reflectionProbes.size() + 1));
    put(data, 0x60, PTR_FOLLOWS); // GfxReflectionProbeInstance[]
    put(data, 0x68, PTR_FOLLOWS); // four GfxSH9Color vectors per probe
}

void EmitReflectionProbes(ZoneWriter &writer, const Mesh &mesh)
{
    if (mesh.reflectionProbes.empty())
        throw std::runtime_error("Replay reflection-probe table is empty");

    writer.align(7);
    for (size_t index = 0; index < mesh.reflectionProbes.size(); ++index)
    {
        const auto &source = mesh.reflectionProbes[index];
        uint8_t probe[48]{};
        put(probe, 0x00, PTR_FOLLOWS); // livePath
        std::memcpy(probe + 0x08, source.origin.data(), sizeof(source.origin));
        put(probe, 0x20, PTR_FOLLOWS); // probeInstances[]
        put(probe, 0x28, static_cast<uint16_t>(index == 0 ? 2 : 1));
        put(probe, 0x2A, std::numeric_limits<uint16_t>::max());
        writer.write(probe, sizeof(probe));
    }
    for (size_t index = 0; index < mesh.reflectionProbes.size(); ++index)
    {
        writer.writeStr("_e" + std::to_string(index) + "_p15");
        writer.align(1);
        if (!index)
        {
            writer.writeT<uint16_t>(0);
            writer.writeT<uint16_t>(1);
        }
        else
            writer.writeT(static_cast<uint16_t>(index + 1));
    }

    const auto emitInstance = [&](const ReflectionProbe &source, const uint16_t imageIndex,
                                  const bool fallback) {
        uint8_t instance[144]{};
        put(instance, 0x00, PTR_FOLLOWS); // livePath
        std::memcpy(instance + 0x08, source.origin.data(), sizeof(source.origin));
        put(instance, 0x14, imageIndex);
        instance[0x17] = fallback ? 1 : 0;
        put(instance, 0x24, 1.0f); // identity quaternion W
        const auto center = fallback ? source.origin : source.volume.midpoint;
        const std::array<float, 3> halfSize =
            fallback ? std::array<float, 3>{131072.0f, 131072.0f, 131072.0f}
                     : source.volume.halfSize;
        std::memcpy(instance + 0x28, center.data(), sizeof(center));
        constexpr std::array<float, 3> xAxis{1.0f, 0.0f, 0.0f};
        constexpr std::array<float, 3> yAxis{0.0f, 1.0f, 0.0f};
        constexpr std::array<float, 3> zAxis{0.0f, 0.0f, 1.0f};
        std::memcpy(instance + 0x34, xAxis.data(), sizeof(xAxis));
        std::memcpy(instance + 0x40, yAxis.data(), sizeof(yAxis));
        std::memcpy(instance + 0x4C, zAxis.data(), sizeof(zAxis));
        std::memcpy(instance + 0x58, halfSize.data(), sizeof(halfSize));
        put(instance, 0x64,
            fallback ? std::numeric_limits<float>::lowest() : 10.0f);
        const std::array<float, 3> feather =
            fallback ? std::array<float, 3>{8.0f, 8.0f, 8.0f}
                     : std::array<float, 3>{1.0f, 1.0f, 4.0f};
        std::memcpy(instance + 0x68, feather.data(), sizeof(feather));
        writer.write(instance, sizeof(instance));
    };

    writer.align(7);
    emitInstance(mesh.reflectionProbes.front(), 0, true);
    for (size_t index = 0; index < mesh.reflectionProbes.size(); ++index)
        emitInstance(mesh.reflectionProbes[index], static_cast<uint16_t>(index), false);
    writer.writeStr("");
    for (size_t index = 0; index < mesh.reflectionProbes.size(); ++index)
        writer.writeStr("b" + std::to_string(index) + "_e" + std::to_string(index) + "_p15");

    writer.align(63);
    for (const auto &probe : mesh.reflectionProbes)
        for (const auto &channel : probe.sh)
            writer.write(channel.data(), channel.size() * sizeof(channel.front()));
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
        put(material, 0x40, PTR_FOLLOWS);
        if (!m.images.empty())
            put(material, 0x48, PTR_FOLLOWS);
        if (!m.constants.empty())
            put(material, 0x50, PTR_FOLLOWS);
        if (!m.decalVolumeMaterial.empty())
            put(material, 0x58, PTR_FOLLOWS);
        if (!m.bufferIndices.empty())
            put(material, 0x60, PTR_FOLLOWS);
        if (!m.buffers.empty())
            put(material, 0x68, PTR_FOLLOWS);
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
        if (!m.constants.empty())
        {
            w.align(15);
            w.write(m.constants.data(), m.constants.size());
        }
        if (!m.decalVolumeMaterial.empty())
        {
            w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
            w.align(7);
            uint8_t decal[0x68]{};
            put(decal, 0, PTR_FOLLOWS);
            w.write(decal, sizeof(decal));
            w.pushStream(XFILE_BLOCK_VIRTUAL);
            w.writeStr(("," + m.decalVolumeMaterial).c_str());
            w.popStream();
            w.popStream();
        }
        if (!m.bufferIndices.empty())
            w.write(m.bufferIndices.data(), m.bufferIndices.size());
        if (!m.buffers.empty())
        {
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
    }
    w.popStream();
    w.popStream();
}
void RegisterImageDefinition(ZoneWriter &w, const Image &image,
                             std::set<std::pair<unsigned, std::string>> &registered)
{
    if (!registered.emplace(19, image.name).second)
        return;
    const Image *definition = &image;
    w.add(static_cast<IW8_XAssetType>(19), image.name, [definition](ZoneWriter &out) {
        const auto &image = *definition;
        // Exact Replay Load_GfxImage E2FD20: 232 bytes, name in virtual,
        // resident pixel payload in TEMP_PRELOAD (E2FFF0), aligned to 16.
        // Image_LoadPixels 19387D0 creates the native GPU texture; no handles
        // or pointers from a captured process belong in this disk asset.
        out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
        out.align(15);
        uint8_t h[0xE8]{};
        put(h, 0, PTR_FOLLOWS);
        put(h, 0x14, uint32_t(image.format));
        put(h, 0x18, image.flags ? image.flags : uint32_t(image.mipCount > 1 ? 1 : 3));
        put(h, 0x1C, uint32_t(image.pixels.size()));
        put(h, 0x24, image.width);
        put(h, 0x26, image.height);
        put(h, 0x28, image.depth);
        put(h, 0x2A, image.numElements);
        h[0x2E] = image.semantic;
        h[0x2F] = image.category;
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
}

void RegisterDecalVolumeDefinition(ZoneWriter &w, const DecalVolumeMaterial &definition,
                                   std::set<std::pair<unsigned, std::string>> &registered)
{
    if (definition.name.empty() ||
        !registered.emplace(ASSET_TYPE_DECAL_VOLUME_MATERIAL, definition.name).second)
        return;
    w.add(ASSET_TYPE_DECAL_VOLUME_MATERIAL, definition.name,
          [definition](ZoneWriter &out) {
        out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
        out.align(7);
        uint8_t volume[iw8sz::DECAL_VOLUME_MATERIAL]{};
        put(volume, 0x00, PTR_FOLLOWS);
        for (size_t channel = 0; channel < definition.channels.size(); ++channel)
            if (!definition.channels[channel].empty())
                put(volume, 0x08 + channel * sizeof(uint64_t), PTR_FOLLOWS);
        put(volume, 0x38, definition.flags);
        std::memcpy(volume + 0x3C, definition.colorTint.data(),
                    sizeof(definition.colorTint));
        put(volume, 0x48, definition.alphaDissolveParms);
        put(volume, 0x4C, definition.emissiveScale);
        put(volume, 0x50, definition.packedDisplacementScaleAndBias);
        put(volume, 0x54, definition.displacementCutoffDistance);
        put(volume, 0x58, definition.displacementCutoffFalloff);
        put(volume, 0x5C, definition.packedTemperatureBaseAndScale);
        volume[0x60] = definition.rows;
        volume[0x61] = definition.columns;
        out.write(volume, sizeof(volume));
        out.pushStream(XFILE_BLOCK_VIRTUAL);
        out.writeStr(definition.name.c_str());
        for (const auto &channel : definition.channels)
            if (!channel.empty())
            {
                out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
                out.align(15);
                uint8_t image[iw8sz::IMAGE]{};
                put(image, 0, PTR_FOLLOWS);
                out.write(image, sizeof(image));
                out.pushStream(XFILE_BLOCK_VIRTUAL);
                out.writeStr(("," + channel).c_str());
                out.popStream();
                out.popStream();
            }
        out.popStream();
        out.popStream();
        out.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
        out.align(7);
        out.reserveCalc(iw8sz::DECAL_VOLUME_MATERIAL);
        for (const auto &channel : definition.channels)
            if (!channel.empty())
            {
                out.align(15);
                out.reserveCalc(iw8sz::IMAGE);
            }
        out.popStream();
    });
}

void RegisterMaterialDefinition(ZoneWriter &w, const Material &m,
                                std::set<std::pair<unsigned, std::string>> &registered,
                                bool registerImages = true)
{
    if (m.techset.empty())
        return;
    // Keep every dependency at the top level: Replay supports only two nested
    // asset patch-memory frames. Techniques themselves are not XAssets.
    if (registerImages)
        for (const auto &image : m.imageDefinitions)
            RegisterImageDefinition(w, image, registered);
    RegisterDecalVolumeDefinition(w, m.decalVolumeDefinition, registered);
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
    if (!m.techniques.empty() && registered.emplace(ASSET_TYPE_TECHSET, m.techset).second)
        w.add(ASSET_TYPE_TECHSET, m.techset,
              [name = m.techset, header = m.techsetHeader,
               techniques = m.techniques](ZoneWriter &out) {
            out.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
            out.align(7);
            auto h = header;
            put(h.data(), 0, PTR_FOLLOWS);
            put(h.data(), 56, PTR_FOLLOWS);
            out.write(h.data(), h.size());
            out.pushStream(XFILE_BLOCK_VIRTUAL);
            out.writeStr(name.c_str());
            out.align(7);
            for (size_t index = 0; index < techniques.size(); ++index)
                out.writeT<uint64_t>(PTR_FOLLOWS);
            for (const auto &t : techniques)
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
            for (const auto &t : techniques)
                for (const auto &s : t.shaders)
                    if (!s.empty())
                    {
                        out.align(7);
                        out.reserveCalc(40);
                    }
            out.popStream();
        });
    if (!registered.emplace(ASSET_TYPE_MATERIAL, m.material).second)
        return;
    // Images and shaders already own their payload in separate asset entries.
    // Keep only the fields consumed by EmitMaterial; retaining the entire input
    // here duplicates every resident image for each material and techset.
    Material definition;
    definition.material = m.material;
    definition.materialInfo = m.materialInfo;
    definition.constants = m.constants;
    definition.bufferIndices = m.bufferIndices;
    definition.textureHeaders = m.textureHeaders;
    definition.techset = m.techset;
    definition.images = m.images;
    definition.decalVolumeMaterial = m.decalVolumeMaterial;
    definition.buffers = m.buffers;
    w.add(ASSET_TYPE_MATERIAL, m.material,
          [definition = std::move(definition)](ZoneWriter &out) {
        EmitMaterial(out, definition, true);
        out.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
        out.align(7);
        out.reserveCalc(120);
        out.align(7);
        out.reserveCalc(64);
        if (!definition.decalVolumeMaterial.empty())
        {
            out.align(7);
            out.reserveCalc(0x68);
        }
        for (size_t index = 0; index < definition.images.size(); ++index)
        {
            out.align(15);
            out.reserveCalc(0xE8);
        }
        out.popStream();
    });
}
void RegisterMaterial(ZoneWriter &w, const Mesh &mesh)
{
    std::set<std::pair<unsigned, std::string>> registered;
    RegisterMaterialDefinition(w, mesh, registered);
    for (const auto &material : mesh.additionalMaterials)
        RegisterMaterialDefinition(w, material, registered);
    for (const auto &material : mesh.assetMaterials)
        RegisterMaterialDefinition(w, material, registered);
}
void RegisterMaterialReference(ZoneWriter &writer, const std::string &name)
{
    writer.add(ASSET_TYPE_MATERIAL, name, [name](ZoneWriter &out) {
        Material reference;
        reference.material = name;
        EmitMaterial(out, reference, false);
    });
}
void RegisterMapMaterials(ZoneWriter &main, ZoneWriter &techsets, const Mesh &mesh)
{
    std::set<std::pair<unsigned, std::string>> mainRegistered;
    std::set<std::pair<unsigned, std::string>> techsetRegistered;
    const auto registerOne = [&](const Material &material) {
        if (material.techset.empty())
            return;
        for (const auto &image : material.imageDefinitions)
            RegisterImageDefinition(main, image, mainRegistered);
        if (mainRegistered.emplace(ASSET_TYPE_MATERIAL, material.material).second)
            RegisterMaterialReference(main, material.material);
        RegisterMaterialDefinition(techsets, material, techsetRegistered, false);
    };
    registerOne(mesh);
    for (const auto &material : mesh.additionalMaterials)
        registerOne(material);
    for (const auto &material : mesh.assetMaterials)
        registerOne(material);
}
void RegisterReflectionProbeImage(ZoneWriter &w, const Mesh &mesh)
{
    std::set<std::pair<unsigned, std::string>> registered;
    RegisterImageDefinition(w, mesh.reflectionProbeArrayImage, registered);
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

std::vector<uint8_t> BuildUmbraTome(const Mesh &m, const StaticModels &staticModels)
{
    BuildStaticModelLayout(staticModels);
    const uint32_t worldSurfaceCount = m.worldSurfaceCount();
    const uint32_t staticModelCount = static_cast<uint32_t>(staticModels.instances.size());
    const uint32_t objectCount = worldSurfaceCount + staticModelCount;
    if (!objectCount || objectCount > 0x01000000 ||
        uint64_t(worldSurfaceCount) * 56 > m.bounds.size())
        throw std::runtime_error("Replay world surfaces cannot be represented by Umbra object IDs");

    const unsigned indexWidth = objectIndexWidth(objectCount);
    const auto packedObjects = packObjectIndices(objectCount, indexWidth);
    std::vector<uint8_t> tome(kUmbraHeaderSize);
    put(tome.data(), 0x00, kUmbraVersion);
    put(tome.data(), 0x0C, 128.0f);
    put(tome.data(), 0x40, objectCount);
    put(tome.data(), 0x54, indexWidth);
    put(tome.data(), 0x5C, objectCount);
    put(tome.data(), 0x8C, uint32_t(1));
    put(tome.data(), 0x90, uint32_t(1));
    put(tome.data(), 0x168, 1.0f);

    float minimum[3], maximum[3];
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        minimum[axis] = m.sceneBounds.midpoint[axis] - m.sceneBounds.halfSize[axis];
        maximum[axis] = m.sceneBounds.midpoint[axis] + m.sceneBounds.halfSize[axis];
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
            minimum[axis] >= maximum[axis])
            throw std::runtime_error("Replay world has invalid Umbra bounds");
    }
    for (const auto &instance : staticModels.instances)
    {
        const auto bounds = TransformBounds(staticModels.models[instance.model].bounds, instance);
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], bounds.midpoint[axis] - bounds.halfSize[axis]);
            maximum[axis] = std::max(maximum[axis], bounds.midpoint[axis] + bounds.halfSize[axis]);
        }
    }
    std::memcpy(tome.data() + 0x14, minimum, sizeof(minimum));
    std::memcpy(tome.data() + 0x20, maximum, sizeof(maximum));
    std::memcpy(tome.data() + 0x150, minimum, sizeof(minimum));
    std::memcpy(tome.data() + 0x15C, maximum, sizeof(maximum));

    const uint32_t topTree = allocate16(tome, 4);
    put(tome.data(), topTree, uint32_t(3));
    const uint32_t topMap = allocate16(tome, 4);
    put(tome.data(), topMap, uint32_t(0));
    put(tome.data(), 0x2C, uint32_t(33));
    put(tome.data(), 0x30, topTree);
    put(tome.data(), 0x34, topMap);

    const uint32_t objectBounds = allocate16(tome, uint64_t(objectCount) * 24);
    const uint32_t objectDistances = allocate16(tome, uint64_t(objectCount) * 32);
    for (uint32_t object = 0; object < objectCount; ++object)
    {
        float midpoint[3], halfSize[3];
        if (object < worldSurfaceCount)
        {
            const auto *bounds = m.bounds.data() + uint64_t(object) * 56;
            std::memcpy(midpoint, bounds, sizeof(midpoint));
            std::memcpy(halfSize, bounds + 12, sizeof(halfSize));
        }
        else
        {
            const auto &instance = staticModels.instances[object - worldSurfaceCount];
            const auto bounds =
                TransformBounds(staticModels.models[instance.model].bounds, instance);
            std::memcpy(midpoint, bounds.midpoint.data(), sizeof(midpoint));
            std::memcpy(halfSize, bounds.halfSize.data(), sizeof(halfSize));
        }
        float objectMinimum[3], objectMaximum[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            objectMinimum[axis] = midpoint[axis] - halfSize[axis];
            objectMaximum[axis] = midpoint[axis] + halfSize[axis];
            if (!std::isfinite(objectMinimum[axis]) || !std::isfinite(objectMaximum[axis]) ||
                objectMinimum[axis] > objectMaximum[axis])
                throw std::runtime_error("Replay surface has invalid Umbra bounds");
        }
        auto *objectBox = tome.data() + objectBounds + uint64_t(object) * 24;
        std::memcpy(objectBox, objectMinimum, sizeof(objectMinimum));
        std::memcpy(objectBox + 12, objectMaximum, sizeof(objectMaximum));
        auto *distanceBox = tome.data() + objectDistances + uint64_t(object) * 32;
        std::memcpy(distanceBox, objectMinimum, sizeof(objectMinimum));
        put(distanceBox, 12, 0.0f);
        std::memcpy(distanceBox + 16, objectMaximum, sizeof(objectMaximum));
        put(distanceBox, 28, std::numeric_limits<float>::infinity());
    }
    put(tome.data(), 0x44, objectBounds);
    put(tome.data(), 0x48, objectDistances);

    const uint32_t userIDs = allocate16(tome, uint64_t(objectCount) * 4);
    for (uint32_t object = 0; object < objectCount; ++object)
    {
        const uint32_t userID =
            object < worldSurfaceCount ? object : 0x10000000u + object - worldSurfaceCount;
        put(tome.data(), userIDs + uint64_t(object) * 4, userID);
    }
    put(tome.data(), 0x50, userIDs);

    const uint32_t objectLists = allocate16(tome, packedObjects.size());
    std::memcpy(tome.data() + objectLists, packedObjects.data(), packedObjects.size());
    put(tome.data(), 0x58, objectLists);

    const uint32_t cellStarts = allocate16(tome, 8);
    put(tome.data(), cellStarts, uint32_t(0));
    put(tome.data(), cellStarts + 4, uint32_t(1));
    put(tome.data(), 0x88, cellStarts);

    const uint32_t tileLodLevels = allocate16(tome, 4);
    put(tome.data(), tileLodLevels, 1.0f);
    put(tome.data(), 0x9C, tileLodLevels);
    const uint32_t tileOffsets = allocate16(tome, 4);
    put(tome.data(), 0xA0, tileOffsets);
    const uint32_t tilePortalExpands = allocate16(tome, 4);
    put(tome.data(), tilePortalExpands, 0.0f);
    put(tome.data(), 0x14C, tilePortalExpands);

    const uint32_t tile = allocate16(tome, kUmbraTileHeaderSize);
    std::memcpy(tome.data() + tile, minimum, sizeof(minimum));
    std::memcpy(tome.data() + tile + 12, maximum, sizeof(maximum));
    put(tome.data(), tile + 0x18, uint32_t(33));
    put(tome.data(), tile + 0x30, 0.0f);
    put(tome.data(), tile + 0x34, uint32_t(1));

    const uint32_t tileTree = allocate16(tome, 4);
    put(tome.data(), tileTree, uint32_t(3));
    put(tome.data(), tile + 0x1C, tileTree - tile);
    const uint32_t tileMap = allocate16(tome, 8);
    put(tome.data(), tileMap, uint32_t(0));
    put(tome.data(), tile + 0x20, tileMap - tile);
    const uint32_t cellIndices = allocate16(tome, 8);
    put(tome.data(), cellIndices, uint32_t(0));
    put(tome.data(), cellIndices + 4, UINT32_MAX);
    put(tome.data(), tile + 0x40, cellIndices - tile);
    const uint32_t cell = allocate16(tome, 36);
    put(tome.data(), cell + 0x08, uint32_t(0));
    put(tome.data(), cell + 0x0C, objectCount);
    put(tome.data(), cell + 0x10, UINT32_MAX);
    put(tome.data(), cell + 0x14, uint32_t(0x80000000));
    put(tome.data(), cell + 0x18, uint32_t(0));
    put(tome.data(), cell + 0x1C, uint32_t(0x0000FFFF));
    put(tome.data(), cell + 0x20, UINT32_MAX);
    put(tome.data(), tile + 0x38, cell - tile);
    align16(tome);
    put(tome.data(), tile + 0x2C, (static_cast<uint32_t>(tome.size()) - tile) << 8 | uint32_t(3));
    put(tome.data(), tileOffsets, tile);

    put(tome.data(), 0x08, static_cast<uint32_t>(tome.size()));
    put(tome.data(), 0x04, crc32c(tome.data() + 8, tome.size() - 8));
    validateUmbraTome(tome, objectCount, worldSurfaceCount, indexWidth);
    return tome;
}

void EmitSortedSurfaces(ZoneWriter &w, const Mesh &m, const StaticModels &staticModels)
{
    if (m.count)
    {
        w.align(3);
        for (unsigned i = 0; i < m.words() * 32; ++i)
            w.writeT<uint32_t>(i < m.count ? i : 0);
    }

    if (!staticModels.instances.empty())
    {
        std::vector<uint16_t> indices(staticModels.instances.size());
        std::iota(indices.begin(), indices.end(), uint16_t{});
        std::stable_sort(
            indices.begin(), indices.end(), [&](const uint16_t left, const uint16_t right) {
                return staticModels.instances[left].model < staticModels.instances[right].model;
            });
        w.align(1);
        const size_t paddedCount = ((staticModels.instances.size() + 31) & ~size_t(31)) + 1;
        for (size_t index = 0; index < paddedCount; ++index)
            w.writeT(index < indices.size() ? indices[index] : uint16_t{});
    }

    if (m.count)
    {
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
}
} // namespace replayrender
