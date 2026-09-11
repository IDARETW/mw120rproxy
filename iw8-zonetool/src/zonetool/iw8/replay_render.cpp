#include "replay_render.h"
#include "../../common/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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

Image LoadImageDefinition(const std::string &path, const nlohmann::json &source,
                          const bool cubemap)
{
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
    const auto pixels = source.at("rgba8").get<std::string>();
    const uint32_t mapType = image.flags & 0x38000u;
    if (!image.name.starts_with("mw120r/") || image.name.size() > 128 || !width || !height ||
        width > 4096 || height > 4096 || !depth || !numElements || depth > 4096 ||
        numElements > 2048 || semantic > UINT8_MAX || category > UINT8_MAX || !image.mipCount ||
        image.mipCount > 13 || image.format < 6 || image.format > 7 ||
        mapType != (cubemap ? 0x8000u : 0u) ||
        std::filesystem::path(pixels).filename() != pixels ||
        pixels.find("..") != std::string::npos)
        throw std::runtime_error("Invalid resident Replay image definition");
    if (cubemap && (width != height || depth != 1 || numElements != 1))
        throw std::runtime_error("Replay reflection image must be one square cubemap");

    size_t length = 0;
    unsigned mipWidth = width, mipHeight = height;
    const size_t slices = cubemap ? 6u : image.numElements;
    for (unsigned level = 0; level < image.mipCount; ++level)
    {
        length += size_t(mipWidth) * mipHeight * 4 * slices;
        if (level + 1 < image.mipCount && mipWidth == 1 && mipHeight == 1)
            throw std::runtime_error("Mip count exceeds dimensions");
        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);
    }
    const auto pixelPath = std::filesystem::path(path).parent_path() / pixels;
    std::error_code error;
    if (length > UINT32_MAX || !std::filesystem::is_regular_file(pixelPath, error) ||
        std::filesystem::file_size(pixelPath) != length)
        throw std::runtime_error("RGBA8 image length does not match dimensions");
    image.width = static_cast<uint16_t>(width);
    image.height = static_cast<uint16_t>(height);
    image.depth = static_cast<uint16_t>(depth);
    image.numElements = static_cast<uint16_t>(numElements);
    image.semantic = static_cast<uint8_t>(semantic);
    image.category = static_cast<uint8_t>(category);
    image.pixels.resize(length);
    std::ifstream input(pixelPath, std::ios::binary);
    if (!input.read(reinterpret_cast<char *>(image.pixels.data()), length))
        throw std::runtime_error("Cannot read RGBA8 image pixels");
    return image;
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
                Image image = LoadImageDefinition(path, im, false);
                if (std::any_of(m.imageDefinitions.begin(), m.imageDefinitions.end(),
                                [&](const Image &x) { return x.name == image.name; }))
                    throw std::runtime_error("Duplicate Replay image definition");
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
                result[size_t(bitOffset + bit) >> 3] |=
                    uint8_t(1u << ((bitOffset + bit) & 7));
        bitOffset += width;
    }
    return result;
}

uint32_t unpackObjectIndex(const std::vector<uint8_t> &data, uint32_t offset,
                           uint64_t bitOffset, unsigned width)
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
                       unsigned width)
{
    if (data.size() < kUmbraHeaderSize || data.size() > UINT32_MAX ||
        read32(data, 0) != kUmbraVersion || read32(data, 8) != data.size())
        throw std::runtime_error("Invalid conservative Umbra header");
    if (read32(data, 4) != crc32c(data.data() + 8, data.size() - 8))
        throw std::runtime_error("Invalid conservative Umbra checksum");
    if (read32(data, 0x2C) != 33 || read32(data, 0x38) != 0 ||
        read32(data, 0x40) != objectCount || read32(data, 0x4C) != 0 ||
        read32(data, 0x54) != width || read32(data, 0x5C) != objectCount ||
        read32(data, 0x7C) != 0 || read32(data, 0x8C) != 1 ||
        read32(data, 0x90) != 1 || read32(data, 0x94) != 0)
        throw std::runtime_error("Invalid conservative Umbra topology");

    validateRange(data, read32(data, 0x30), 4, "top-level tree");
    validateRange(data, read32(data, 0x34), 4, "top-level map");
    validateRange(data, read32(data, 0x44), uint64_t(objectCount) * 24, "object bounds");
    validateRange(data, read32(data, 0x48), uint64_t(objectCount) * 32,
                  "object distances");
    validateRange(data, read32(data, 0x50), uint64_t(objectCount) * 4, "user IDs");
    validateRange(data, read32(data, 0x58),
                  (uint64_t(objectCount) * width + 31) / 32 * 4, "object list");
    validateRange(data, read32(data, 0x88), 8, "cell starts");
    validateRange(data, read32(data, 0x9C), 4, "tile LOD");
    validateRange(data, read32(data, 0xA0), 4, "tile table");
    validateRange(data, read32(data, 0x14C), 4, "tile portal expansion");
    if (read32(data, read32(data, 0x30)) != 3 || read32(data, read32(data, 0x34)) != 0 ||
        read32(data, read32(data, 0x88)) != 0 ||
        read32(data, read32(data, 0x88) + 4) != 1 ||
        readFloat(data, read32(data, 0x9C)) != 1.0f ||
        readFloat(data, read32(data, 0x14C)) != 0.0f)
        throw std::runtime_error("Invalid conservative Umbra tile metadata");

    const uint32_t userIDs = read32(data, 0x50);
    const uint32_t objectLists = read32(data, 0x58);
    for (uint32_t object = 0; object < objectCount; ++object)
        if (read32(data, userIDs + object * 4) != object ||
            unpackObjectIndex(data, objectLists, uint64_t(object) * width, width) != object)
            throw std::runtime_error("Conservative Umbra object mapping is not lossless");

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
    if (read32(data, cell) != 0 || read32(data, cell + 4) != 0 ||
        read32(data, cell + 8) != 0 || read32(data, cell + 12) != objectCount ||
        read32(data, cell + 16) != UINT32_MAX || read32(data, cell + 20) != 0x80000000 ||
        read32(data, cell + 24) != 0 || read32(data, cell + 28) != 0x0000FFFF ||
        read32(data, cell + 32) != UINT32_MAX)
        throw std::runtime_error("Invalid conservative Umbra cell");
}
} // namespace

replaybounds::Bounds LoadBounds(const nlohmann::json &value)
{
    if (!value.is_array() || value.size() != 2 || !value[0].is_array() ||
        !value[1].is_array() || value[0].size() != 3 || value[1].size() != 3)
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
        const double required = std::max(maximum - bounds.midpoint[axis],
                                         bounds.midpoint[axis] - minimum);
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
    const auto &reflectionProbes = j.at("reflectionProbes");
    if (!reflectionProbes.is_array() || reflectionProbes.empty() ||
        reflectionProbes.size() > 256)
        throw std::runtime_error("Invalid Replay reflection-probe table");
    std::set<std::string> reflectionImages;
    for (const auto &source : reflectionProbes)
    {
        ReflectionProbe probe;
        const auto origin = vector(source.at("origin"), 3);
        std::copy(origin.begin(), origin.end(), probe.origin.begin());
        probe.volume = LoadBounds(source.at("volume"));
        probe.image = LoadImageDefinition(path, source.at("image"), true);
        if (!reflectionImages.insert(probe.image.name).second)
            throw std::runtime_error("Duplicate Replay reflection-probe image");
        const auto &sh = source.at("sh");
        if (!sh.is_array() || sh.size() != probe.sh.size())
            throw std::runtime_error("Replay reflection probe requires four SH vectors");
        for (std::size_t channel = 0; channel < probe.sh.size(); ++channel)
        {
            if (!sh.at(channel).is_array() ||
                sh.at(channel).size() != probe.sh[channel].size())
                throw std::runtime_error("Replay reflection-probe SH vector has invalid width");
            for (std::size_t coefficient = 0; coefficient < probe.sh[channel].size();
                 ++coefficient)
            {
                probe.sh[channel][coefficient] =
                    sh.at(channel).at(coefficient).get<float>();
                if (!std::isfinite(probe.sh[channel][coefficient]))
                    throw std::runtime_error("Replay reflection-probe SH value is non-finite");
            }
        }
        m.reflectionProbes.push_back(std::move(probe));
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
        for (const auto &tree : trees)
        {
            const auto bounds = LoadBounds(tree.at("bounds"));
            std::vector<unsigned> owned = tree.at("surfaces").get<std::vector<unsigned>>();
            std::ranges::sort(owned);
            owned.erase(std::unique(owned.begin(), owned.end()), owned.end());
            for (std::size_t begin = 0; begin < owned.size();)
            {
                if (owned[begin] >= m.worldSurfaceCount())
                    throw std::runtime_error("Replay DPVS cell references an invalid surface");
                std::size_t end = begin + 1;
                while (end < owned.size() && owned[end] == owned[end - 1] + 1)
                    ++end;
                leaves.push_back(
                    {bounds, owned[begin], static_cast<unsigned>(end - begin), 0, 0});
                begin = end;
            }
        }
        if (leaves.size() > 65535)
            throw std::runtime_error("Replay DPVS cell exceeds the native AABB child limit");
        if (leaves.size() <= 1)
        {
            cell.trees = std::move(leaves);
        }
        else
        {
            cell.trees.push_back(
                {cell.bounds, 0, 0, 48, static_cast<uint16_t>(leaves.size())});
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
void RegisterImageDefinition(ZoneWriter &w, const Image &image,
                             std::set<std::pair<unsigned, std::string>> &registered)
{
    if (!registered.emplace(19, image.name).second)
        return;
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
            image.flags ? image.flags
                        : uint32_t(image.mipCount > 1 ? 1 : 3));
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

void RegisterMaterialDefinition(ZoneWriter &w, const Material &m,
                                std::set<std::pair<unsigned, std::string>> &registered)
{
    if (m.techset.empty())
        return;
    // Keep every dependency at the top level: Replay supports only two nested
    // asset patch-memory frames. Techniques themselves are not XAssets.
    for (const auto &image : m.imageDefinitions)
        RegisterImageDefinition(w, image, registered);
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

std::vector<uint8_t> BuildUmbraTome(const Mesh &m)
{
    const uint32_t objectCount = m.worldSurfaceCount();
    if (!objectCount || objectCount > 0x01000000 ||
        uint64_t(objectCount) * 56 > m.bounds.size())
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
        const auto *bounds = m.bounds.data() + uint64_t(object) * 56;
        float midpoint[3], halfSize[3];
        std::memcpy(midpoint, bounds, sizeof(midpoint));
        std::memcpy(halfSize, bounds + 12, sizeof(halfSize));
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
        put(tome.data(), userIDs + uint64_t(object) * 4, object);
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
    put(tome.data(), tile + 0x2C,
        (static_cast<uint32_t>(tome.size()) - tile) << 8 | uint32_t(3));
    put(tome.data(), tileOffsets, tile);

    put(tome.data(), 0x08, static_cast<uint32_t>(tome.size()));
    put(tome.data(), 0x04, crc32c(tome.data() + 8, tome.size() - 8));
    validateUmbraTome(tome, objectCount, indexWidth);
    return tome;
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
