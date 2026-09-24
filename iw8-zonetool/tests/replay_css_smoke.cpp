#include "zonetool/iw8/replay_sunshadow.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct Node
{
    std::size_t offset;
    unsigned depth;
};

std::uint32_t read32(const std::vector<std::uint8_t> &bytes, const std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t))
        throw std::runtime_error("compressed shadow read exceeds payload");
    std::uint32_t value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::uint16_t read16(const std::vector<std::uint8_t> &bytes, const std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint16_t))
        throw std::runtime_error("compressed shadow read exceeds payload");
    std::uint16_t value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

float depth(const std::vector<std::uint8_t> &bytes, const std::size_t offset)
{
    const float value = std::bit_cast<float>(read32(bytes, offset));
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
        throw std::runtime_error("compressed shadow depth is outside [0, 1]");
    return value;
}

void validateTile(const std::vector<std::uint8_t> &bytes, const std::size_t start,
                  const std::size_t end, std::array<std::uint64_t, 4> &types,
                  const std::uint32_t marker = 0x00020000,
                  std::vector<std::size_t> *depthOffsets = nullptr)
{
    if (end < start || end - start < 80 || read32(bytes, start) != end - start ||
        read32(bytes, start + 12) != end - start || read32(bytes, start + 4) != 512 ||
        std::bit_cast<float>(read32(bytes, start + 8)) != 1.0f / 512.0f ||
        read32(bytes, start + 16) != marker)
        throw std::runtime_error("invalid 512-pixel compressed shadow tile header");
    const float depthOrigin = std::bit_cast<float>(read32(bytes, start + 24));
    const float inverseDepthSpan = std::bit_cast<float>(read32(bytes, start + 28));
    if (!std::isfinite(depthOrigin) || !std::isfinite(inverseDepthSpan) ||
        (marker ? inverseDepthSpan <= 0.0f
                : depthOrigin != 0.0f || inverseDepthSpan != 0.0f))
        throw std::runtime_error("invalid compressed shadow tile depth header");

    std::vector<Node> pending;
    std::vector<std::pair<std::size_t, std::size_t>> records{{start, start + 80}};
    for (unsigned root = 0; root != 4; ++root)
    {
        const std::size_t offset = start + 32 + 12 * root;
        depth(bytes, offset + 4);
        depth(bytes, offset + 8);
        if (depthOffsets)
        {
            depthOffsets->push_back(offset + 4);
            depthOffsets->push_back(offset + 8);
        }
        pending.push_back({offset, 0});
    }
    constexpr std::array<std::size_t, 4> childSizes{0, 4, 8, 4};
    while (!pending.empty())
    {
        const Node node = pending.back();
        pending.pop_back();
        if (node.depth > 12 || node.offset + 4 > end)
            throw std::runtime_error("compressed shadow node exceeds tile or tree depth");
        const std::uint32_t packed = read32(bytes, node.offset);
        const std::size_t base = node.offset + (packed >> 8);
        std::size_t advance = 0;
        for (unsigned child = 0; child != 4; ++child)
        {
            const unsigned type = (packed >> (2 * child)) & 3;
            if (type)
            {
                const std::size_t offset = base + advance;
                if (offset <= node.offset || offset > end || end - offset < childSizes[type])
                    throw std::runtime_error("compressed shadow child offset exceeds tile");
                records.emplace_back(offset, offset + childSizes[type]);
                ++types[type];
                if (type == 1 || type == 2)
                    pending.push_back({offset, node.depth + 1});
                if (type == 2)
                {
                    depth(bytes, offset + 4);
                    if (depthOffsets)
                        depthOffsets->push_back(offset + 4);
                }
                if (type == 3)
                {
                    depth(bytes, offset);
                    if (depthOffsets)
                        depthOffsets->push_back(offset);
                }
            }
            advance += childSizes[type];
        }
    }
    std::sort(records.begin(), records.end());
    std::size_t cursor = start;
    for (const auto &[recordStart, recordEnd] : records)
    {
        if (recordStart != cursor)
            throw std::runtime_error("compressed shadow tree has a gap or overlapping record");
        cursor = recordEnd;
    }
    if (cursor != end)
        throw std::runtime_error("compressed shadow tile has unreferenced record bytes");
}

struct TreeSample
{
    float threshold;
    std::size_t depthOffset;
};

TreeSample sampleTree(const std::vector<std::uint8_t> &bytes, const std::size_t start,
                      const std::size_t end, const std::size_t x, const std::size_t y)
{
    if (x >= 512 || y >= 512 || end < start || end - start < 80)
        throw std::runtime_error("compressed shadow sample is outside mini tile");
    const unsigned root = ((x >> 8) & 1) | (((y >> 8) & 1) << 1);
    std::size_t node = start + 32 + 12 * root;
    std::size_t inheritedDepth = node + 4;
    constexpr std::array<std::size_t, 4> childSizes{0, 4, 8, 4};
    for (unsigned level = 0; level <= 12; ++level)
    {
        const std::uint32_t packed = read32(bytes, node);
        const unsigned bit = level < 8 ? 7 - level : 0;
        const unsigned child = (level < 8 ? (x >> bit) & 1 : 0) |
                               (level < 8 ? ((y >> bit) & 1) << 1 : 0);
        const unsigned type = (packed >> (2 * child)) & 3;
        if (type == 0)
            return {depth(bytes, inheritedDepth), inheritedDepth};
        std::size_t next = node + (packed >> 8);
        for (unsigned preceding = 0; preceding < child; ++preceding)
            next += childSizes[(packed >> (2 * preceding)) & 3];
        if (next <= node || next > end || end - next < childSizes[type])
            throw std::runtime_error("compressed shadow sample child exceeds mini tile");
        if (type == 3)
            return {depth(bytes, next), next};
        if (type == 2)
            inheritedDepth = next + 4;
        node = next;
    }
    throw std::runtime_error("compressed shadow sample exceeds tree depth");
}

void testTreeSampler()
{
    std::vector<std::uint8_t> tile(104);
    const auto put32 = [&](const std::size_t offset, const std::uint32_t value)
    {
        std::memcpy(tile.data() + offset, &value, 4);
    };
    const auto putDepth = [&](const std::size_t offset, const float value)
    {
        put32(offset, std::bit_cast<std::uint32_t>(value));
    };
    put32(0, 104);
    put32(4, 512);
    putDepth(8, 1.0f / 512.0f);
    put32(12, 104);
    put32(16, 0x00020000);
    putDepth(24, 1.0f);
    putDepth(28, 0.5f);
    put32(32, (48 << 8) | 0xe4); // root children: inherited, branch, branch+depth, leaf
    putDepth(36, 0.11f);
    putDepth(40, 0.12f);
    for (unsigned root = 1; root < 4; ++root)
    {
        putDepth(32 + 12 * root + 4, 0.22f + 0.11f * (root - 1));
        putDepth(32 + 12 * root + 8, 0.5f);
    }
    put32(80, (16 << 8) | (3 << 6)); // type 1 inherits until child 3
    put32(84, (16 << 8) | (1 << 2)); // type 2 overrides inherited depth
    putDepth(88, 0.66f);
    putDepth(92, 0.77f);
    putDepth(96, 0.88f);
    put32(100, 0); // type 1 below type 2 inherits its depth
    std::array<std::uint64_t, 4> types{};
    validateTile(tile, 0, tile.size(), types);
    const auto check = [&](const unsigned x, const unsigned y, const std::size_t offset)
    {
        const auto sample = sampleTree(tile, 0, tile.size(), x, y);
        if (sample.depthOffset != offset || sample.threshold != depth(tile, offset))
            throw std::runtime_error("compressed shadow synthetic tree sample differs");
    };
    check(0, 0, 36);     // type 0 inherits root +4
    check(128, 0, 36);   // type 1, then type 0, preserves root depth
    check(192, 64, 96); // type 1, then type 3, uses leaf depth
    check(0, 128, 88);  // type 2, then type 0, uses new inherited depth
    check(64, 128, 88); // type 2, type 1, then type 0, still inherits
    check(128, 128, 92); // type 3 reads its own float
    check(256, 0, 48);  // high x bit selects root 1
    check(0, 256, 60);  // high y bit selects root 2
    check(511, 511, 72); // high x and y bits select root 3
    std::cout << "synthetic compressed shadow tree sampling passed\n";
}

std::vector<std::uint8_t> encodeExact(const std::vector<std::uint8_t> &image)
{
    constexpr std::size_t resolution = 512;
    if (image.size() != resolution * resolution * sizeof(float))
        throw std::runtime_error("exact-depth input must be 512x512 R32_FLOAT");
    std::vector<float> depths(resolution * resolution);
    std::memcpy(depths.data(), image.data(), image.size());
    const std::array tiles{replaysunshadow::EncodeTile(depths, 0.0f, 1.0f)};
    auto forest = replaysunshadow::EncodeForest(512, 1, 1, 0.0f, 1.0f, tiles);

    // The reader is independent of the production encoder. Check both the
    // complete native record graph and every point sample before writing.
    std::array<std::uint64_t, 4> types{};
    validateTile(forest, 36, forest.size(), types);
    for (std::size_t y = 0; y < resolution; ++y)
        for (std::size_t x = 0; x < resolution; ++x)
        {
            const auto sample = sampleTree(forest, 36, forest.size(), x, y);
            if (std::bit_cast<std::uint32_t>(sample.threshold) !=
                read32(image, 4 * (y * resolution + x)))
                throw std::runtime_error("exact-depth CPU sample mismatch at (" +
                                         std::to_string(x) + "," + std::to_string(y) + ")");
        }
    return forest;
}
void testExactEncoder()
{
    std::vector<std::uint8_t> image(512 * 512 * 4);
    const auto set = [&](const unsigned x, const unsigned y, const std::uint32_t bits)
    {
        std::memcpy(image.data() + 4 * (y * 512 + x), &bits, 4);
    };
    set(255, 255, std::bit_cast<std::uint32_t>(0.125f));
    set(256, 255, std::bit_cast<std::uint32_t>(0.25f));
    set(255, 256, std::bit_cast<std::uint32_t>(0.5f));
    set(256, 256, std::bit_cast<std::uint32_t>(0.75f));
    set(0, 0, std::bit_cast<std::uint32_t>(1.0f));
    set(511, 511, 0x80000000u); // negative zero must retain its exact bits
    set(127, 128, std::bit_cast<std::uint32_t>(0.625f));
    const auto forest = encodeExact(image);
    if (read32(forest, 20) != 0x00010001 || read32(forest, 32) != 0x80000024u ||
        read32(forest, 36) != forest.size() - 36 ||
        read32(forest, 36 + 24) != 0 || read32(forest, 36 + 28) != 0x3f800000u)
        throw std::runtime_error("exact-depth forest header differs from native layout");
    const auto check = [&](const unsigned x, const unsigned y)
    {
        if (std::bit_cast<std::uint32_t>(sampleTree(forest, 36, forest.size(), x, y).threshold) !=
            read32(image, 4 * (y * 512 + x)))
            throw std::runtime_error("exact-depth boundary or sparse-hole sample differs");
    };
    for (const auto &[x, y] : std::array<std::pair<unsigned, unsigned>, 11>{
             {{255, 255}, {256, 255}, {255, 256}, {256, 256},
              {254, 255}, {257, 255}, {255, 254}, {255, 257},
              {0, 0}, {511, 511}, {127, 128}}})
        check(x, y);
    set(3, 4, 0x7fc00000u);
    bool rejected = false;
    try { (void)encodeExact(image); }
    catch (const std::runtime_error &) { rejected = true; }
    if (!rejected)
        throw std::runtime_error("exact-depth encoder accepted NaN input");
    set(3, 4, std::bit_cast<std::uint32_t>(1.01f));
    rejected = false;
    try { (void)encodeExact(image); }
    catch (const std::runtime_error &) { rejected = true; }
    if (!rejected)
        throw std::runtime_error("exact-depth encoder accepted out-of-range input");
    std::cout << "synthetic exact-depth CSS encoding passed (" << forest.size()
              << " bytes, 512x512 bit-exact samples)\n";
}

void testForestEncoder(const std::filesystem::path &output = {})
{
    constexpr unsigned resolution = 2048, width = 3, height = 2;
    std::array<std::vector<std::uint8_t>, width * height> tiles;
    std::vector<float> reference(resolution * resolution);
    std::vector<float> image(512 * 512);
    for (unsigned cell = 0; cell < tiles.size(); ++cell)
    {
        if (cell == 0 || cell == 4)
            continue;
        for (unsigned y = 0; y < 512; ++y)
            for (unsigned x = 0; x < 512; ++x)
            {
                float value = cell == 1 ? (x + y + 1) / 1024.0f
                                  : cell == 2 ? 0.125f
                                  : cell == 3 ? (x < 256 ? 0.25f : 0.75f)
                                              : ((x + y) % 5 ? 0.625f : 0.0f);
                // Cell 1's offset-range GPU case at receiver 0.1249 lands
                // exactly on this float. Simplifying its normalization
                // expression rounds lower and must fail the consumer test.
                if (cell == 1 && x == 0 && y == 0)
                    value = std::bit_cast<float>(0x3e9ff974u);
                image[y * 512 + x] = value;
                reference[(cell / width * 512 + y) * resolution + cell % width * 512 + x] = value;
            }
        tiles[cell] = replaysunshadow::EncodeTile(image, 0, 1);
    }
    const auto forest = replaysunshadow::EncodeForest(resolution, width, height, 0, 1, tiles);
    if (read32(forest, 4) != resolution || read32(forest, 20) != 0x00020003 ||
        read32(forest, 32) != 0 || read32(forest, 32 + 4 * 4) != 0)
        throw std::runtime_error("cropped forest header or empty cell differs");
    std::size_t next = 32 + tiles.size() * 4;
    std::array<std::uint64_t, 4> types{};
    for (unsigned cell = 0; cell < tiles.size(); ++cell)
    {
        if (tiles[cell].empty())
            continue;
        const std::size_t start = read32(forest, 32 + 4 * cell) & 0x7fffffffu;
        if (start != next)
            throw std::runtime_error("cropped forest tile pointer differs");
        next += tiles[cell].size();
        validateTile(forest, start, next, types);
        for (unsigned y = 0; y < 512; ++y)
            for (unsigned x = 0; x < 512; ++x)
                if (sampleTree(forest, start, next, x, y).threshold !=
                    reference[(cell / width * 512 + y) * resolution + cell % width * 512 + x])
                    throw std::runtime_error("cropped forest CPU sample differs");
    }
    if (next != forest.size())
        throw std::runtime_error("cropped forest has trailing bytes");
    const auto rejects = [](auto action) {
        try { action(); }
        catch (const std::runtime_error &) { return; }
        throw std::runtime_error("compressed shadow encoder accepted invalid parameters");
    };
    rejects([&] { replaysunshadow::EncodeForest(512, width, height, 0, 1, tiles); });
    rejects([&] { replaysunshadow::EncodeForest(resolution, width, 1, 0, 1, tiles); });
    rejects([&] { replaysunshadow::EncodeForest(resolution, width, height, 0, 0, tiles); });
    rejects([&] { replaysunshadow::EncodeTile(image, NAN, 1); });
    rejects([&] { replaysunshadow::EncodeTile(image, 0, -1); });
    tiles[1][16] = 1;
    rejects([&] { replaysunshadow::EncodeForest(resolution, width, height, 0, 1, tiles); });
    if (!output.empty())
    {
        std::filesystem::create_directories(output);
        std::ofstream css(output / "forest.css", std::ios::binary);
        std::ofstream depths(output / "forest.r32", std::ios::binary);
        if (!css.write(reinterpret_cast<const char *>(forest.data()), forest.size()) ||
            !depths.write(reinterpret_cast<const char *>(reference.data()),
                           reference.size() * sizeof(float)))
            throw std::runtime_error("cannot write cropped forest fixture");
    }
    std::cout << "cropped 3x2 forest passed: 4 tiles, 2 empty cells, 2048 logical resolution, "
              << forest.size() << " bytes\n";
}

void testCasterBake()
{
    replaysunshadow::Scene scene;
    scene.materials.resize(2);
    scene.materials[0].cullMode = scene.materials[1].cullMode = 0;
    auto &mask = scene.materials[1];
    mask.alphaTest = 1;
    mask.width = mask.height = 16;
    mask.alpha.resize(16 * 16);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 8; x < 16; ++x)
            mask.alpha[y * 16 + x] = 255;
    const auto quad = [](float z, unsigned material) {
        replaysunshadow::Surface surface;
        surface.material = material;
        const std::array<replaysunshadow::Vertex, 4> corners{{
            {{16, 16, z}, {0, 0}, 1}, {{112, 16, z}, {1, 0}, 1},
            {{112, 112, z}, {1, 1}, 1}, {{16, 112, z}, {0, 1}, 1}}};
        // Replay submits clockwise winding. The extracted IW3 BSP triangle
        // order already matches this shadow-raster contract.
        for (const unsigned index : {0u, 2u, 1u, 0u, 3u, 2u})
            surface.vertices.push_back(corners[index]);
        return surface;
    };
    scene.surfaces = {quad(20, 0), quad(50, 1)};
    const replaybounds::Bounds receivers{{64, 64, 25}, {64, 64, 25}};
    const auto checkBake = [&](const replaysunshadow::Data &bake, bool invertedAlpha) {
        const auto &params = bake.parameters;
        if (params.resolution != 512 || params.centerX != -256 || params.centerY != 256 ||
            params.sampleSize != 1 || params.forestSize != 1 || params.reserved != 0x10000 ||
            params.nearPlane != 0 || params.farPlane != 0 || params.flags != 0)
            throw std::runtime_error("vertical-sun caster crop differs from target projection");
        const std::size_t start = read32(bake.bytes, 32) & 0x7fffffffu;
        std::array<std::uint64_t, 4> types{};
        validateTile(bake.bytes, start, bake.bytes.size(), types);
        const float origin = std::bit_cast<float>(read32(bake.bytes, start + 24));
        const float scale = std::bit_cast<float>(read32(bake.bytes, start + 28));
        if (origin < 1)
            throw std::runtime_error("shadow range clips non-casting receivers below the casters");
        for (const float worldX : {32.5f, 96.5f})
        {
            // For sun +Z the native X-seed basis is right=-Y, up=+X.
            // The crop starts at (-512,-512); inspect pixel centers far from
            // the bilinear alpha boundary and from triangle edges.
            const auto sample = sampleTree(bake.bytes, start, bake.bytes.size(),
                                            512 - 49, static_cast<unsigned>(512 - worldX));
            const float worldDepth = sample.threshold / scale - origin;
            const bool opaqueMask = (worldX > 64) != invertedAlpha;
            if (std::abs(worldDepth - (opaqueMask ? 50.0f : 20.0f)) > 0.0001f)
                throw std::runtime_error("native caster bake lost alpha coverage or nearest depth");
        }
        if (sampleTree(bake.bytes, start, bake.bytes.size(), 0, 0).threshold != 0)
            throw std::runtime_error("native caster bake filled an empty pixel");
    };
    checkBake(replaysunshadow::Bake(scene, {0, 0, 1}, receivers), false);
    mask.atlasTile = true;
    checkBake(replaysunshadow::Bake(scene, {0, 0, 1}, receivers), false);
    mask.alphaTest = 3;
    checkBake(replaysunshadow::Bake(scene, {0, 0, 1}, receivers), true);
    // A submitted +Z-facing triangle remains front-facing after Replay's
    // right/up projection and the viewport Y flip.
    replaysunshadow::Scene culling;
    culling.materials.resize(1);
    culling.surfaces = {quad(20, 0)};
    for (const unsigned cull : {1u, 2u})
    {
        culling.materials[0].cullMode = cull;
        const auto bake = replaysunshadow::Bake(culling, {0, 0, 1}, receivers);
        if ((read32(bake.bytes, 32) != 0) != (cull == 1))
            throw std::runtime_error("caster projection reversed material face culling");
    }
    // Exercise the other seed branch with a +X sun and a +X-facing plane.
    culling.materials[0].cullMode = 1;
    for (auto &vertex : culling.surfaces[0].vertices)
        vertex.position = {vertex.position[2], vertex.position[0], vertex.position[1]};
    const auto horizontal = replaysunshadow::Bake(culling, {1, 0, 0}, receivers);
    const auto horizontalStart = read32(horizontal.bytes, 32) & 0x7fffffffu;
    if (horizontal.parameters.centerX != 256 || horizontal.parameters.centerY != 256 ||
        horizontalStart == 0 ||
        std::abs(sampleTree(horizontal.bytes, horizontalStart, horizontal.bytes.size(),
                            32, 512 - 49).threshold * 130.0f - 21.0f) > 0.0001f)
        throw std::runtime_error("horizontal-sun caster projection differs");
    scene.surfaces[1].vertices[0].position[0] = NAN;
    bool rejected = false;
    try { (void)replaysunshadow::Bake(scene, {0, 0, 1}, receivers); }
    catch (const std::runtime_error &) { rejected = true; }
    if (!rejected)
        throw std::runtime_error("caster bake accepted a nonfinite position");
    std::cout << "native caster bake passed: both projection seeds, crop, face culling, nearest depth, direct/atlas alpha, invalid geometry\n";
}

std::string firstTopologyDifference(const std::vector<std::uint8_t> &native,
                                    const std::vector<std::uint8_t> &shader)
{
    struct Pair
    {
        std::size_t nativeOffset, shaderOffset;
        unsigned x, y, size;
        std::string path;
    };
    std::vector<Pair> pending;
    for (unsigned root = 4; root-- > 0;)
        pending.push_back({32 + 12 * root, 32 + 12 * root,
                           (root & 1) * 256, (root >> 1) * 256, 256,
                           "r" + std::to_string(root)});
    constexpr std::array<std::size_t, 4> childSizes{0, 4, 8, 4};
    while (!pending.empty())
    {
        const auto pair = pending.back();
        pending.pop_back();
        const auto a = read32(native, pair.nativeOffset);
        const auto b = read32(shader, pair.shaderOffset);
        auto nextA = pair.nativeOffset + (a >> 8);
        auto nextB = pair.shaderOffset + (b >> 8);
        std::vector<Pair> children;
        for (unsigned child = 0; child != 4; ++child)
        {
            const auto typeA = (a >> (2 * child)) & 3;
            const auto typeB = (b >> (2 * child)) & 3;
            if (typeA != typeB)
                return pair.path + std::to_string(child) + " rect=(" +
                       std::to_string(pair.x + (child & 1) * pair.size / 2) + "," +
                       std::to_string(pair.y + (child >> 1) * pair.size / 2) +
                       ") " + std::to_string(pair.size / 2) + "x" +
                       std::to_string(pair.size / 2) + " stock=" +
                       std::to_string(typeA) + " shader=" + std::to_string(typeB);
            if (typeA == 1 || typeA == 2)
                children.push_back({nextA, nextB,
                                    pair.x + (child & 1) * pair.size / 2,
                                    pair.y + (child >> 1) * pair.size / 2,
                                    pair.size / 2, pair.path + std::to_string(child)});
            nextA += childSizes[typeA];
            nextB += childSizes[typeB];
        }
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            pending.push_back(*it);
    }
    return {};
}

void validate(const std::string &path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("cannot open " + path);
    const auto length = input.tellg();
    if (length < 32 || length > 0x7fffffff)
        throw std::runtime_error("invalid compressed shadow payload length");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(bytes.data()), length))
        throw std::runtime_error("cannot read " + path);

    const std::size_t resolution = read32(bytes, 4);
    const std::size_t width = read16(bytes, 20);
    const std::size_t height = read16(bytes, 22);
    const std::size_t tableEnd = 32 + 4 * width * height;
    if (read32(bytes, 0) != bytes.size() || read32(bytes, 12) != bytes.size() ||
        !resolution || std::bit_cast<float>(read32(bytes, 8)) != 1.0f / resolution ||
        read32(bytes, 16) != 0x00030009 || !width || !height || tableEnd > bytes.size())
        throw std::runtime_error("invalid compressed shadow forest header");
    const float depthOrigin = std::bit_cast<float>(read32(bytes, 24));
    const float inverseDepthSpan = std::bit_cast<float>(read32(bytes, 28));
    if (!std::isfinite(depthOrigin) || !std::isfinite(inverseDepthSpan) ||
        inverseDepthSpan <= 0.0f)
        throw std::runtime_error("invalid compressed shadow forest depth header");

    std::vector<std::size_t> tiles;
    std::size_t empty = 0;
    std::size_t scalar = 0;
    for (std::size_t cell = 0; cell < width * height; ++cell)
    {
        const std::uint32_t entry = read32(bytes, 32 + 4 * cell);
        if (!entry)
            ++empty;
        else if (entry & 0x80000000u)
        {
            const std::size_t offset = entry & 0x7fffffffu;
            if (offset < tableEnd || offset >= bytes.size() || (offset & 3) ||
                (!tiles.empty() && offset <= tiles.back()))
                throw std::runtime_error("invalid compressed shadow forest pointer");
            tiles.push_back(offset);
        }
        else
        {
            depth(bytes, 32 + 4 * cell);
            ++scalar;
        }
    }
    if (!tiles.empty() && tiles.front() != tableEnd)
        throw std::runtime_error("compressed shadow tile data does not follow forest table");
    std::array<std::uint64_t, 4> types{};
    for (std::size_t index = 0; index < tiles.size(); ++index)
        validateTile(bytes, tiles[index],
                     index + 1 < tiles.size() ? tiles[index + 1] : bytes.size(), types);
    if (tiles.empty() && tableEnd != bytes.size())
        throw std::runtime_error("compressed shadow payload has unreferenced bytes");
    std::cout << path << ": " << width << 'x' << height << " forest, " << tiles.size()
              << " tiles, " << scalar << " scalar, " << empty << " empty; children "
              << types[1] << '/' << types[2] << '/' << types[3] << '\n';
}
} // namespace

int main(int argc, char **argv)
{
    const bool tileMode = argc >= 2 && std::string(argv[1]) == "--tile";
    const bool shaderTileMode = argc >= 2 && std::string(argv[1]) == "--shader-tile";
    const bool compareMode = argc >= 2 && std::string(argv[1]) == "--compare-shader-tile";
    const bool scalarMode = argc >= 2 && std::string(argv[1]) == "--sample-scalar";
    const bool treeMode = argc >= 2 && std::string(argv[1]) == "--sample-tree";
    const bool encodeMode = argc >= 2 && std::string(argv[1]) == "--encode-exact";
    const bool forestMode = argc >= 2 && std::string(argv[1]) == "--forest-fixture";
    const bool selfTestMode = argc >= 2 && std::string(argv[1]) == "--self-test";
    if (argc < 2 || ((tileMode || shaderTileMode) && argc != 3) ||
        (compareMode && argc != 6) || (scalarMode && argc != 6) ||
        (treeMode && argc != 8) || (encodeMode && argc != 4) ||
        (selfTestMode && argc != 2) || (forestMode && argc != 3))
    {
        std::cerr << "usage: css-smoke <raw compressed-sun-shadow payload> [...]\n"
                     "       css-smoke --tile <native 512-pixel mini-tile>\n"
                     "       css-smoke --shader-tile <Replay shader readback>\n"
                     "       css-smoke --compare-shader-tile <stock forest> <x> <y> <shader tile>\n"
                     "       css-smoke --sample-scalar <stock forest> <x> <y> <projected depth>\n"
                     "       css-smoke --sample-tree <stock forest> <cell x> <cell y> <pixel x> <pixel y> <projected depth>\n"
                     "       css-smoke --encode-exact <512x512 depth.r32> <out.css>\n"
                     "       css-smoke --forest-fixture <output-directory>\n"
                     "       css-smoke --self-test\n";
        return 2;
    }
    try
    {
        if (selfTestMode)
        {
            testTreeSampler();
            testExactEncoder();
            testForestEncoder();
            testCasterBake();
            return 0;
        }
        if (forestMode)
        {
            testForestEncoder(argv[2]);
            return 0;
        }
        if (encodeMode)
        {
            std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
            if (!input || input.tellg() != 512 * 512 * 4)
                throw std::runtime_error("exact-depth input must be 512x512 R32_FLOAT");
            std::vector<std::uint8_t> image(512 * 512 * 4);
            input.seekg(0);
            if (!input.read(reinterpret_cast<char *>(image.data()), image.size()))
                throw std::runtime_error("cannot read exact-depth input");
            const auto forest = encodeExact(image);
            std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
            if (!output || !output.write(reinterpret_cast<const char *>(forest.data()),
                                         forest.size()))
                throw std::runtime_error(std::string("cannot write ") + argv[3]);
            std::cout << argv[3] << ": " << forest.size()
                      << " bytes; 512x512 CPU point samples bit-exact\n";
            return 0;
        }
        if (treeMode)
        {
            std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
            if (!input || input.tellg() < 32 || input.tellg() > 0x7fffffff)
                throw std::runtime_error("invalid compressed shadow forest");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(input.tellg()));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
                throw std::runtime_error("cannot read compressed shadow forest");
            const std::size_t width = read16(bytes, 20);
            const std::size_t height = read16(bytes, 22);
            const std::size_t cellX = std::stoul(argv[3]);
            const std::size_t cellY = std::stoul(argv[4]);
            const std::size_t pixelX = std::stoul(argv[5]);
            const std::size_t pixelY = std::stoul(argv[6]);
            const float projectedDepth = std::stof(argv[7]);
            if (!width || !height || cellX >= width || cellY >= height ||
                32 + 4 * width * height > bytes.size() ||
                read32(bytes, 16) != 0x00030009 || !std::isfinite(projectedDepth))
                throw std::runtime_error("invalid compressed shadow forest sample");
            const std::uint32_t entry = read32(bytes, 32 + 4 * (cellY * width + cellX));
            const std::size_t start = entry & 0x7fffffffu;
            if (!(entry & 0x80000000u) || start < 32 + 4 * width * height ||
                start > bytes.size() || bytes.size() - start < 80)
                throw std::runtime_error("forest cell does not contain a valid mini tile");
            const std::size_t length = read32(bytes, start);
            if (length < 80 || length > bytes.size() - start)
                throw std::runtime_error("compressed shadow mini tile exceeds forest");
            std::array<std::uint64_t, 4> types{};
            validateTile(bytes, start, start + length, types);
            const auto sample = sampleTree(bytes, start, start + length, pixelX, pixelY);
            const float origin = std::bit_cast<float>(read32(bytes, start + 24));
            const float inverseSpan = std::bit_cast<float>(read32(bytes, start + 28));
            const float receiver = (projectedDepth + origin) * inverseSpan;
            const float globalReceiver = projectedDepth + std::bit_cast<float>(read32(bytes, 24));
            const bool lit = sample.threshold == 0.0f || globalReceiver <= 0.000015f ||
                             receiver >= sample.threshold;
            std::cout << argv[2] << " cell(" << cellX << ',' << cellY << ") pixel("
                      << pixelX << ',' << pixelY << "): receiver=" << receiver
                      << ", tree=" << sample.threshold << " at +"
                      << sample.depthOffset - start << ", "
                      << (lit ? "lit" : "shadowed") << '\n';
            return 0;
        }
        if (scalarMode)
        {
            std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
            if (!input || input.tellg() < 32 || input.tellg() > 0x7fffffff)
                throw std::runtime_error("invalid compressed shadow forest");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(input.tellg()));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
                throw std::runtime_error("cannot read compressed shadow forest");
            const std::size_t width = read16(bytes, 20);
            const std::size_t height = read16(bytes, 22);
            const std::size_t x = std::stoul(argv[3]);
            const std::size_t y = std::stoul(argv[4]);
            if (!width || !height || x >= width || y >= height ||
                32 + 4 * width * height > bytes.size() ||
                read32(bytes, 16) != 0x00030009)
                throw std::runtime_error("invalid compressed shadow forest cell");
            const std::uint32_t entry = read32(bytes, 32 + 4 * (y * width + x));
            if (!entry || (entry & 0x80000000u))
                throw std::runtime_error("forest cell does not contain a scalar depth");
            const float threshold = depth(bytes, 32 + 4 * (y * width + x));
            const float projectedDepth = std::stof(argv[5]);
            const float origin = std::bit_cast<float>(read32(bytes, 24));
            const float inverseSpan = std::bit_cast<float>(read32(bytes, 28));
            if (!std::isfinite(projectedDepth) || !std::isfinite(origin) ||
                !std::isfinite(inverseSpan) || inverseSpan <= 0.0f)
                throw std::runtime_error("invalid compressed shadow depth parameters");
            // Replay cs_sunvis.434.cso's non-tagged forest path: outer +24/+28
            // normalize the projected receiver depth before the scalar test.
            // The bias term is omitted here; its runtime constant is view-specific.
            const float receiver = (projectedDepth + origin) * inverseSpan;
            const bool lit = projectedDepth + origin <= 0.000015f || receiver >= threshold;
            std::cout << argv[2] << " (" << x << ',' << y << "): receiver="
                      << receiver << ", scalar=" << threshold << ", "
                      << (lit ? "lit" : "shadowed") << '\n';
            return 0;
        }
        if (compareMode)
        {
            const auto readFile = [](const char *path)
            {
                std::ifstream input(path, std::ios::binary | std::ios::ate);
                if (!input || input.tellg() < 80 || input.tellg() > 0x7fffffff)
                    throw std::runtime_error(std::string("invalid compressed shadow file: ") + path);
                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(input.tellg()));
                input.seekg(0);
                if (!input.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
                    throw std::runtime_error(std::string("cannot read ") + path);
                return bytes;
            };
            const auto forest = readFile(argv[2]);
            const std::size_t width = read16(forest, 20);
            const std::size_t height = read16(forest, 22);
            const std::size_t x = std::stoul(argv[3]);
            const std::size_t y = std::stoul(argv[4]);
            if (!width || !height || x >= width || y >= height ||
                32 + 4 * width * height > forest.size())
                throw std::runtime_error("compressed shadow forest tile is out of bounds");
            const std::uint32_t entry = read32(forest, 32 + 4 * (y * width + x));
            const std::size_t offset = entry & 0x7fffffffu;
            if (!(entry & 0x80000000u) || offset < 32 + 4 * width * height ||
                offset > forest.size() || forest.size() - offset < 80)
                throw std::runtime_error("stock forest cell has no valid mini tile");
            const std::size_t length = read32(forest, offset);
            if (length < 80 || length > forest.size() - offset)
                throw std::runtime_error("stock mini tile exceeds forest");
            std::vector<std::uint8_t> native(forest.begin() + offset,
                                             forest.begin() + offset + length);
            auto shader = readFile(argv[5]);
            std::array<std::uint64_t, 4> nativeTypes{}, shaderTypes{};
            std::vector<std::size_t> nativeDepths, shaderDepths;
            validateTile(native, 0, native.size(), nativeTypes, 0x00020000,
                         &nativeDepths);
            validateTile(shader, 0, shader.size(), shaderTypes, 0,
                         &shaderDepths);
            std::sort(nativeDepths.begin(), nativeDepths.end());
            std::sort(shaderDepths.begin(), shaderDepths.end());
            if (native.size() != shader.size() || nativeDepths != shaderDepths)
            {
                const auto first = firstTopologyDifference(native, shader);
                throw std::runtime_error(first.empty() ? "native and shader tile trees differ"
                                                       : "native and shader tile trees differ at " + first);
            }
            const float inverseSpan = std::bit_cast<float>(read32(native, 28));
            float maximumWorldError = 0;
            for (const std::size_t depthOffset : nativeDepths)
            {
                maximumWorldError = std::max(maximumWorldError,
                    std::abs(depth(native, depthOffset) - depth(shader, depthOffset)) /
                        inverseSpan);
                std::fill_n(native.begin() + depthOffset, 4, std::uint8_t{0});
                std::fill_n(shader.begin() + depthOffset, 4, std::uint8_t{0});
            }
            for (const std::size_t headerOffset : {16u, 24u, 28u})
            {
                std::fill_n(native.begin() + headerOffset, 4, std::uint8_t{0});
                std::fill_n(shader.begin() + headerOffset, 4, std::uint8_t{0});
            }
            if (native != shader || maximumWorldError > 0.003f)
                throw std::runtime_error("native and shader tile records differ");
            std::cout << argv[5] << ": native tree and records match; "
                      << nativeDepths.size() << " depths, max world error "
                      << maximumWorldError << '\n';
            return 0;
        }
        if (tileMode || shaderTileMode)
        {
            std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error(std::string("cannot open ") + argv[2]);
            const auto length = input.tellg();
            if (length < 80 || length > 0x7fffffff)
                throw std::runtime_error("invalid compressed shadow tile length");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char *>(bytes.data()), length))
                throw std::runtime_error("cannot read compressed shadow tile");
            std::array<std::uint64_t, 4> types{};
            validateTile(bytes, 0, bytes.size(), types,
                         shaderTileMode ? 0 : 0x00020000);
            std::cout << argv[2] << (shaderTileMode ? ": shader tile; children "
                                                  : ": native tile; children ")
                      << types[1] << '/'
                      << types[2] << '/' << types[3] << '\n';
            return 0;
        }
        for (int index = 1; index < argc; ++index)
            validate(argv[index]);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
