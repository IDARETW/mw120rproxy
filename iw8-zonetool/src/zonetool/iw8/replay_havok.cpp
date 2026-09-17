#include "replay_havok.h"
#include "replay_opaque_strings.h"

#include "../../common/fs_util.h"
#include "../../common/log.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace iw8::havok
{
namespace
{

constexpr char kReplaySha256[] = "68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0";
constexpr std::uintptr_t kShapeListType = 0x4621020;
constexpr std::uint64_t kReplayTypeCompendiumSignature = 0xC16B2E49324C3540ull;
constexpr std::size_t kMaximumAllocation = 256u * 1024u * 1024u;

template <typename T> T Read(const void *address)
{
    T value{};
    std::memcpy(&value, address, sizeof(value));
    return value;
}

template <typename T> void Write(void *address, const T &value)
{
    std::memcpy(address, &value, sizeof(value));
}

std::uint32_t ReadBigEndian32(const std::uint8_t *address)
{
    return std::uint32_t(address[0]) << 24 | std::uint32_t(address[1]) << 16 |
           std::uint32_t(address[2]) << 8 | std::uint32_t(address[3]);
}

void WriteBigEndian32(std::uint8_t *address, const std::uint32_t value)
{
    address[0] = static_cast<std::uint8_t>(value >> 24);
    address[1] = static_cast<std::uint8_t>(value >> 16);
    address[2] = static_cast<std::uint8_t>(value >> 8);
    address[3] = static_cast<std::uint8_t>(value);
}

struct TagSection
{
    std::size_t offset = 0;
    std::size_t size = 0;
};

TagSection FindTagSection(const std::vector<std::uint8_t> &data, const TagSection parent,
                          const std::string_view name)
{
    if (parent.offset + 8 > data.size() || parent.size < 8 ||
        parent.offset + parent.size > data.size())
        throw std::runtime_error("Native Havok tagfile section is invalid");
    for (std::size_t offset = parent.offset + 8; offset + 8 <= parent.offset + parent.size;)
    {
        const auto size = std::size_t(ReadBigEndian32(data.data() + offset) & 0x0FFFFFFFu);
        if (size < 8 || offset + size > parent.offset + parent.size)
            throw std::runtime_error("Native Havok tagfile child section is invalid");
        if (std::memcmp(data.data() + offset + 4, name.data(), 4) == 0)
            return {offset, size};
        offset += size;
    }
    throw std::runtime_error("Native Havok tagfile is missing " + std::string(name));
}

std::uint64_t ReadHavokVarUInt(const std::vector<std::uint8_t> &data, std::size_t &offset,
                               const std::size_t end)
{
    if (offset >= end)
        throw std::runtime_error("Native Havok type table is truncated");
    const auto prefix = data[offset++];
    std::size_t length = 0;
    std::uint64_t value = 0;
    if ((prefix & 0x80u) == 0)
    {
        length = 1;
        value = prefix & 0x7Fu;
    }
    else if ((prefix & 0xC0u) == 0x80u)
    {
        length = 2;
        value = prefix & 0x3Fu;
    }
    else if ((prefix & 0xE0u) == 0xC0u)
    {
        length = 3;
        value = prefix & 0x1Fu;
    }
    else if ((prefix & 0xF8u) == 0xE0u)
    {
        length = 4;
        value = prefix & 7u;
    }
    else if ((prefix & 0xF8u) == 0xE8u)
    {
        length = 5;
        value = prefix & 7u;
    }
    else if (prefix == 0xF8u)
    {
        length = 6;
    }
    else if ((prefix & 0xF8u) == 0xF0u)
    {
        length = 8;
        value = prefix & 7u;
    }
    else if (prefix == 0xF9u)
    {
        length = 9;
    }
    else
    {
        throw std::runtime_error("Native Havok type table has an invalid integer");
    }
    if (offset + length - 1 > end)
        throw std::runtime_error("Native Havok type table integer is truncated");
    for (std::size_t byte = 1; byte < length; ++byte)
        value = value << 8 | data[offset++];
    return value;
}

std::uint32_t ReplayTypeIndex(const std::string_view identity)
{
    static constexpr std::pair<std::string_view, std::uint32_t> indices[] = {
        {"int", 5},
        {"HavokPhysicsAsset", 7},
        {"T*<hknpPhysicsSystemData>", 8},
        {"hkArray<int, hkContainerHeapAllocator>", 9},
        {"hkArray<hkStringPtr, hkContainerHeapAllocator>", 10},
        {"hknpPhysicsSystemData", 13},
        {"hkStringPtr", 15},
        {"hkArray<hknpMaterial, hkContainerHeapAllocator>", 22},
        {"hkArray<hknpMotionProperties, hkContainerHeapAllocator>", 23},
        {"hkArray<hknpPhysicsSystemData::bodyCinfoWithAttachment, hkContainerHeapAllocator>", 24},
        {"unsigned long long", 31},
        {"hkUint16", 34},
        {"hknpMaterial", 35},
        {"hknpMotionProperties", 37},
        {"hknpPhysicsSystemData::bodyCinfoWithAttachment", 39},
        {"hkUint32", 50},
        {"unsigned int", 59},
        {"char", 163},
        {"hkRefPtr<hknpShape>", 75},
        {"hkRefPtr<hkReferencedObject>", 132},
        {"hknpCompressedMeshShape", 602},
        {"hkArray<hkUint32, hkContainerHeapAllocator>", 608},
        {"hkArray<hkcdSimdTree::Node, hkContainerHeapAllocator>", 283},
        {"hkcdSimdTree::Node", 285},
        {"hknpCompressedMeshShapeData", 1466},
        {"hkArray<unsigned int, hkContainerHeapAllocator>", 536},
        {"hkArray<unsigned long long, hkContainerHeapAllocator>", 537},
        {"hkArray<hkcdDefaultStaticMeshTree::PrimitiveDataRun, hkContainerHeapAllocator>", 538},
        {"hkArray<hkcdStaticMeshTree::Section, hkContainerHeapAllocator>", 540},
        {"hkArray<hkcdStaticMeshTree::Primitive, hkContainerHeapAllocator>", 541},
        {"hkArray<hkUint16, hkContainerHeapAllocator>", 542},
        {"hkcdDefaultStaticMeshTree::PrimitiveDataRun", 545},
        {"hkcdStaticMeshTree::Section", 548},
        {"hkcdStaticMeshTree::Primitive", 550},
        {"hkcdCompressedAabbCodecs::Aabb5BytesCodec", 528},
        {"hkArray<hkcdCompressedAabbCodecs::Aabb5BytesCodec, hkContainerHeapAllocator>", 553},
        {"hkcdCompressedAabbCodecs::Aabb4BytesCodec", 524},
        {"hkArray<hkcdCompressedAabbCodecs::Aabb4BytesCodec, hkContainerHeapAllocator>", 558},
    };
    const auto found = std::ranges::find_if(
        indices, [identity](const auto &entry) { return entry.first == identity; });
    if (found == std::end(indices))
        throw std::runtime_error("Replay global type compendium has no mapped type '" +
                                 std::string(identity) + "'");
    return found->second;
}

std::vector<std::string> ReadTypeIdentities(const std::vector<std::uint8_t> &data,
                                            const TagSection type)
{
    const auto stringsSection = FindTagSection(data, type, "TSTR");
    std::vector<std::string> strings;
    for (std::size_t offset = stringsSection.offset + 8;
         offset < stringsSection.offset + stringsSection.size;)
    {
        const auto *begin = reinterpret_cast<const char *>(data.data() + offset);
        const auto remaining = stringsSection.offset + stringsSection.size - offset;
        const auto *terminator = static_cast<const char *>(std::memchr(begin, 0, remaining));
        if (!terminator)
            throw std::runtime_error("Native Havok type string table is invalid");
        strings.emplace_back(begin, terminator);
        offset += std::size_t(terminator - begin) + 1;
    }

    struct Parameter
    {
        bool type = false;
        std::uint32_t value = 0;
    };
    const auto namesSection = FindTagSection(data, type, "TNA1");
    std::size_t offset = namesSection.offset + 8;
    const auto end = namesSection.offset + namesSection.size;
    const auto count64 = ReadHavokVarUInt(data, offset, end);
    if (count64 == 0 || count64 > 65535)
        throw std::runtime_error("Native Havok type count is invalid");
    const auto count = static_cast<std::size_t>(count64);
    std::vector<std::string> names(count);
    std::vector<std::vector<Parameter>> parameters(count);
    names[0] = "NULL";
    for (std::size_t typeIndex = 1; typeIndex < count; ++typeIndex)
    {
        const auto nameIndex = ReadHavokVarUInt(data, offset, end);
        const auto parameterCount = ReadHavokVarUInt(data, offset, end);
        if (nameIndex >= strings.size() || parameterCount > 64)
            throw std::runtime_error("Native Havok type name is invalid");
        names[typeIndex] = strings[static_cast<std::size_t>(nameIndex)];
        for (std::size_t parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex)
        {
            const auto parameterName = ReadHavokVarUInt(data, offset, end);
            const auto value = ReadHavokVarUInt(data, offset, end);
            if (parameterName >= strings.size() || strings[parameterName].empty() ||
                value > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Native Havok template parameter is invalid");
            const bool isType = strings[parameterName][0] == 't';
            if (!isType && strings[parameterName][0] != 'v')
                throw std::runtime_error("Native Havok template parameter kind is invalid");
            if (isType && value >= count)
                throw std::runtime_error("Native Havok template type is invalid");
            parameters[typeIndex].push_back({isType, static_cast<std::uint32_t>(value)});
        }
    }

    std::vector<std::string> identities(count);
    identities[0] = "NULL";
    std::vector<bool> building(count);
    const auto buildIdentity = [&](const auto &self,
                                   const std::size_t typeIndex) -> const std::string & {
        if (!identities[typeIndex].empty())
            return identities[typeIndex];
        if (building[typeIndex])
            throw std::runtime_error("Native Havok type identity is recursive");
        building[typeIndex] = true;
        auto identity = names[typeIndex];
        if (!parameters[typeIndex].empty())
        {
            identity += '<';
            for (std::size_t index = 0; index < parameters[typeIndex].size(); ++index)
            {
                if (index)
                    identity += ", ";
                const auto &parameter = parameters[typeIndex][index];
                identity +=
                    parameter.type ? self(self, parameter.value) : std::to_string(parameter.value);
            }
            identity += '>';
        }
        building[typeIndex] = false;
        identities[typeIndex] = std::move(identity);
        return identities[typeIndex];
    };
    for (std::size_t typeIndex = 1; typeIndex < count; ++typeIndex)
        buildIdentity(buildIdentity, typeIndex);
    return identities;
}

std::vector<std::uint8_t> CompactPhysicsAssetTagfile(
    const std::vector<std::uint8_t> &selfDescribing)
{
    if (selfDescribing.size() < 8 || std::memcmp(selfDescribing.data() + 4, "TAG0", 4) != 0 ||
        (ReadBigEndian32(selfDescribing.data()) & 0x0FFFFFFFu) != selfDescribing.size())
        throw std::runtime_error("Native model physics tagfile root is invalid");
    const TagSection root{0, selfDescribing.size()};
    const auto type = FindTagSection(selfDescribing, root, "TYPE");
    const auto index = FindTagSection(selfDescribing, root, "INDX");
    if (type.offset + type.size != index.offset)
        throw std::runtime_error("Native model physics tagfile section order is invalid");
    const auto identities = ReadTypeIdentities(selfDescribing, type);

    std::vector<std::uint8_t> compact;
    compact.reserve(selfDescribing.size() - type.size + 32);
    compact.insert(compact.end(), selfDescribing.begin(), selfDescribing.begin() + type.offset);
    const auto compendiumOffset = compact.size();
    compact.resize(compact.size() + 32);
    WriteBigEndian32(compact.data() + compendiumOffset, 0x40000020u);
    std::memcpy(compact.data() + compendiumOffset + 4, "TCRF", 4);
    Write(compact.data() + compendiumOffset + 8, kReplayTypeCompendiumSignature);
    compact.insert(compact.end(), selfDescribing.begin() + index.offset, selfDescribing.end());
    WriteBigEndian32(compact.data(), static_cast<std::uint32_t>(compact.size()));

    const TagSection compactRoot{0, compact.size()};
    const auto compactIndex = FindTagSection(compact, compactRoot, "INDX");
    const auto items = FindTagSection(compact, compactIndex, "ITEM");
    if ((items.size - 8) % 12 != 0)
        throw std::runtime_error("Native model physics item table is invalid");
    for (std::size_t offset = items.offset + 8; offset < items.offset + items.size; offset += 12)
    {
        const auto typeAndFlags = Read<std::uint32_t>(compact.data() + offset);
        const auto localType = typeAndFlags & 0x00FFFFFFu;
        if (!localType)
            continue;
        if (localType >= identities.size())
            throw std::runtime_error("Native model physics item type is invalid");
        Write(compact.data() + offset,
              (typeAndFlags & 0xFF000000u) | ReplayTypeIndex(identities[localType]));
    }

    const auto patches = FindTagSection(compact, compactIndex, "PTCH");
    for (std::size_t offset = patches.offset + 8; offset < patches.offset + patches.size;)
    {
        if (offset + 8 > patches.offset + patches.size)
            throw std::runtime_error("Native model physics patch table is truncated");
        const auto localType = Read<std::uint32_t>(compact.data() + offset);
        const auto count = Read<std::uint32_t>(compact.data() + offset + 4);
        if (!localType || localType >= identities.size() ||
            count > (patches.offset + patches.size - offset - 8) / 4)
            throw std::runtime_error("Native model physics patch record is invalid");
        Write(compact.data() + offset, ReplayTypeIndex(identities[localType]));
        offset += 8 + std::size_t(count) * 4;
    }
    return compact;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path &path)
{
    std::vector<std::uint8_t> bytes;
    if (!zt::read_file(path.string(), bytes))
        throw std::runtime_error("Cannot read " + path.string());
    return bytes;
}

std::string Sha256(const std::vector<std::uint8_t> &bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    DWORD digestBytes = 0;
    std::vector<std::uint8_t> object;
    std::vector<std::uint8_t> digest;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
                          sizeof(objectBytes), &resultBytes, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digestBytes),
                          sizeof(digestBytes), &resultBytes, 0) < 0)
    {
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("Cannot initialize SHA-256");
    }

    object.resize(objectBytes);
    digest.resize(digestBytes);
    const auto cleanup = [&] {
        if (hash)
            BCryptDestroyHash(hash);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
                       0) < 0 ||
        BCryptFinishHash(hash, digest.data(), digestBytes, 0) < 0)
    {
        cleanup();
        throw std::runtime_error("Cannot calculate SHA-256");
    }
    cleanup();

    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const auto byte : digest)
    {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 15]);
    }
    return text;
}

struct Hull
{
    std::vector<std::array<float, 3>> points;
    std::vector<CollisionSlab> slabs;
    std::uint32_t contents = 1;
    std::uint32_t model = 0;
    std::uint32_t surfaceFlags = 0;
    std::uint16_t glassId = 0;
    std::vector<std::array<float, 4>> ladderPlanes;
};

struct MeshTriangle
{
    std::array<std::uint32_t, 3> indices{};
    std::uint32_t contents = 1;
    std::uint32_t surfaceFlags = 0;
    std::uint32_t material = 5;
};

struct Mesh
{
    std::vector<std::array<float, 3>> vertices;
    std::vector<MeshTriangle> triangles;
    std::uint32_t model = 0;
};

struct CollisionInput
{
    std::vector<Hull> hulls;
    std::vector<CollisionModel> models;
    std::vector<Mesh> meshes;
};

CollisionInput ReadCollision(const std::filesystem::path &path)
{
    const auto bytes = ReadFile(path);
    if (bytes.size() < 12 || (std::memcmp(bytes.data(), "MWCOLL02", 8) != 0 &&
                              std::memcmp(bytes.data(), "MWCOLL03", 8) != 0 &&
                              std::memcmp(bytes.data(), "MWCOLL04", 8) != 0 &&
                              std::memcmp(bytes.data(), "MWCOLL05", 8) != 0 &&
                              std::memcmp(bytes.data(), "MWCOLL06", 8) != 0 &&
                              std::memcmp(bytes.data(), "MWCOLL07", 8) != 0 &&
                              std::memcmp(bytes.data(), "MWCOLL08", 8) != 0 &&
                              std::memcmp(bytes.data(), "MWCOLL09", 8) != 0))
        throw std::runtime_error("Collision input must use MWCOLL02 through MWCOLL09");

    const bool hasGlassIds = std::memcmp(bytes.data(), "MWCOLL09", 8) == 0;
    const bool hasMeshes = hasGlassIds || std::memcmp(bytes.data(), "MWCOLL08", 8) == 0;
    const bool hasLadderPlanes = hasMeshes || std::memcmp(bytes.data(), "MWCOLL07", 8) == 0;
    const bool hasSurfaceFlags = hasLadderPlanes || std::memcmp(bytes.data(), "MWCOLL06", 8) == 0;
    const bool hasSlabs = hasSurfaceFlags || std::memcmp(bytes.data(), "MWCOLL05", 8) == 0;
    const bool grouped = hasSlabs || std::memcmp(bytes.data(), "MWCOLL04", 8) == 0;
    const bool tagged = grouped || std::memcmp(bytes.data(), "MWCOLL03", 8) == 0;
    const auto count = Read<std::uint32_t>(bytes.data() + 8);
    if (count == 0 || count > 262144)
        throw std::runtime_error("Collision hull count is invalid");

    std::size_t cursor = 12;
    CollisionInput result;
    std::uint32_t meshCount = 0;
    if (grouped)
    {
        if (cursor + 4 > bytes.size())
            throw std::runtime_error("Collision model table is truncated");
        const auto modelCount = Read<std::uint32_t>(bytes.data() + cursor);
        cursor += 4;
        if (hasMeshes)
        {
            if (cursor + 4 > bytes.size())
                throw std::runtime_error("Collision mesh table is truncated");
            meshCount = Read<std::uint32_t>(bytes.data() + cursor);
            cursor += 4;
            if (meshCount > 65535)
                throw std::runtime_error("Collision mesh count is invalid");
        }
        if (modelCount == 0 || modelCount > std::numeric_limits<std::uint16_t>::max() ||
            cursor + std::size_t(modelCount) * 24 > bytes.size())
            throw std::runtime_error("Collision model count is invalid");
        result.models.resize(modelCount);
        for (auto &model : result.models)
        {
            std::memcpy(model.minimum.data(), bytes.data() + cursor, 12);
            std::memcpy(model.maximum.data(), bytes.data() + cursor + 12, 12);
            cursor += 24;
            for (std::size_t axis = 0; axis < 3; ++axis)
                if (!std::isfinite(model.minimum[axis]) || !std::isfinite(model.maximum[axis]) ||
                    model.minimum[axis] > model.maximum[axis])
                    throw std::runtime_error("Collision model bounds are invalid");
        }
    }
    else
    {
        result.models.resize(1);
        result.models[0].minimum.fill(std::numeric_limits<float>::infinity());
        result.models[0].maximum.fill(-std::numeric_limits<float>::infinity());
    }

    result.hulls.reserve(count);
    constexpr std::uint32_t supportedContents = 0x33691;
    for (std::uint32_t hullIndex = 0; hullIndex < count; ++hullIndex)
    {
        if (cursor + (hasGlassIds      ? 26u
                      : hasLadderPlanes   ? 24u
                      : hasSurfaceFlags ? 20u
                      : hasSlabs        ? 16u
                      : grouped         ? 12u
                      : tagged          ? 8u
                                        : 4u) >
            bytes.size())
            throw std::runtime_error("Collision input is truncated");
        Hull hull;
        const auto vertexCount = Read<std::uint32_t>(bytes.data() + cursor);
        cursor += 4;
        if (tagged)
        {
            hull.contents = Read<std::uint32_t>(bytes.data() + cursor);
            cursor += 4;
        }
        if (grouped)
        {
            hull.model = Read<std::uint32_t>(bytes.data() + cursor);
            cursor += 4;
        }
        std::uint32_t slabCount = 0;
        if (hasSlabs)
        {
            slabCount = Read<std::uint32_t>(bytes.data() + cursor);
            cursor += 4;
        }
        if (hasSurfaceFlags)
        {
            hull.surfaceFlags = Read<std::uint32_t>(bytes.data() + cursor);
            cursor += 4;
        }
        std::uint32_t ladderPlaneCount = 0;
        if (hasLadderPlanes)
        {
            ladderPlaneCount = Read<std::uint32_t>(bytes.data() + cursor);
            cursor += 4;
        }
        if (hasGlassIds)
        {
            hull.glassId = Read<std::uint16_t>(bytes.data() + cursor);
            cursor += 2;
        }
        if (vertexCount < 4 || vertexCount > 252 || hull.contents == 0 ||
            (hull.contents & ~supportedContents) != 0 || hull.model >= result.models.size() ||
            (hull.glassId && !(hull.contents & 0x10u)) ||
            slabCount > 252 || ladderPlaneCount > 8 || (hull.surfaceFlags & ~0x7FFFFu) != 0 ||
            cursor + std::size_t(vertexCount) * 12 + std::size_t(slabCount) * 20 +
                    std::size_t(ladderPlaneCount) * 16 >
                bytes.size())
            throw std::runtime_error("Collision hull data is invalid");
        hull.points.resize(vertexCount);
        std::memcpy(hull.points.data(), bytes.data() + cursor, std::size_t(vertexCount) * 12);
        cursor += std::size_t(vertexCount) * 12;
        for (const auto &point : hull.points)
            for (const auto value : point)
                if (!std::isfinite(value))
                    throw std::runtime_error("Collision contains a non-finite vertex");
        hull.slabs.resize(slabCount);
        for (auto &slab : hull.slabs)
        {
            std::memcpy(slab.direction.data(), bytes.data() + cursor, 12);
            slab.midpoint = Read<float>(bytes.data() + cursor + 12);
            slab.halfSize = Read<float>(bytes.data() + cursor + 16);
            cursor += 20;
            if (!std::isfinite(slab.midpoint) || !std::isfinite(slab.halfSize) ||
                slab.halfSize < 0.0f ||
                std::any_of(
                    slab.direction.begin(), slab.direction.end(),
                    [](const float value) { return !std::isfinite(value); }))
                throw std::runtime_error("Collision trigger slab is invalid");
        }
        hull.ladderPlanes.resize(ladderPlaneCount);
        for (auto &plane : hull.ladderPlanes)
        {
            std::memcpy(plane.data(), bytes.data() + cursor, sizeof(plane));
            cursor += sizeof(plane);
            const float lengthSquared =
                plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2];
            if (std::any_of(plane.begin(), plane.end(),
                            [](const float value) { return !std::isfinite(value); }) ||
                std::abs(plane[2]) > 0.001f || std::abs(lengthSquared - 1.0f) > 0.001f)
                throw std::runtime_error("Collision ladder plane is invalid");
        }
        ++result.models[hull.model].hullCount;
        if (!grouped)
            for (std::size_t axis = 0; axis < 3; ++axis)
                for (const auto &point : hull.points)
                {
                    result.models[0].minimum[axis] =
                        (std::min)(result.models[0].minimum[axis], point[axis]);
                    result.models[0].maximum[axis] =
                        (std::max)(result.models[0].maximum[axis], point[axis]);
                }
        result.hulls.push_back(std::move(hull));
    }
    result.meshes.reserve(meshCount);
    for (std::uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
    {
        if (cursor + 12 > bytes.size())
            throw std::runtime_error("Collision mesh header is truncated");
        const auto vertexCount = Read<std::uint32_t>(bytes.data() + cursor);
        const auto triangleCount = Read<std::uint32_t>(bytes.data() + cursor + 4);
        const auto model = Read<std::uint32_t>(bytes.data() + cursor + 8);
        cursor += 12;
        constexpr std::uint32_t maximumMeshElements = 4'000'000;
        constexpr std::size_t triangleBytes = 24;
        if (vertexCount < 3 || vertexCount > maximumMeshElements || triangleCount == 0 ||
            triangleCount > maximumMeshElements || model >= result.models.size() ||
            cursor + std::size_t(vertexCount) * 12 + std::size_t(triangleCount) * triangleBytes >
                bytes.size())
            throw std::runtime_error("Collision mesh dimensions are invalid");

        Mesh mesh;
        mesh.model = model;
        mesh.vertices.resize(vertexCount);
        std::memcpy(mesh.vertices.data(), bytes.data() + cursor, std::size_t(vertexCount) * 12);
        cursor += std::size_t(vertexCount) * 12;
        for (const auto &point : mesh.vertices)
            for (const auto value : point)
                if (!std::isfinite(value) || std::abs(value) > 1000000.0f)
                    throw std::runtime_error("Collision mesh contains an invalid vertex");

        mesh.triangles.resize(triangleCount);
        for (auto &triangle : mesh.triangles)
        {
            std::memcpy(triangle.indices.data(), bytes.data() + cursor, 12);
            triangle.contents = Read<std::uint32_t>(bytes.data() + cursor + 12);
            triangle.surfaceFlags = Read<std::uint32_t>(bytes.data() + cursor + 16);
            triangle.material = Read<std::uint32_t>(bytes.data() + cursor + 20);
            cursor += triangleBytes;
            if (std::ranges::any_of(
                    triangle.indices,
                    [vertexCount](const auto index) { return index >= vertexCount; }) ||
                triangle.contents == 0 || (triangle.contents & ~supportedContents) != 0 ||
                (triangle.surfaceFlags & ~0x7FFFFu) != 0 || triangle.material > 0x1FFFFu)
                throw std::runtime_error("Collision mesh triangle is invalid");
        }
        result.meshes.push_back(std::move(mesh));
    }
    if (cursor != bytes.size())
        throw std::runtime_error("Collision input has trailing data");
    if (!result.models[0].hullCount)
        throw std::runtime_error("Collision input has no world hulls");
    return result;
}

struct FloorTriangle
{
    std::array<float, 3> origin{};
    std::array<float, 3> u{};
    std::array<float, 3> v{};
    float determinant = 0;
    std::uint32_t material = 5;
};

class FloorMaterials
{
  public:
    explicit FloorMaterials(const std::filesystem::path &path)
    {
        if (path.empty())
            return;
        const auto bytes = ReadFile(path);
        if (bytes.size() < 12 || std::memcmp(bytes.data(), "MWRSTEP1", 8) != 0)
            throw std::runtime_error("Footstep input must use MWRSTEP1");
        const auto count = Read<std::uint32_t>(bytes.data() + 8);
        if (bytes.size() != 12 + std::size_t(count) * 40)
            throw std::runtime_error("Footstep input length is invalid");

        const auto *cursor = bytes.data() + 12;
        for (std::uint32_t i = 0; i < count; ++i, cursor += 40)
        {
            FloorTriangle triangle;
            std::array<float, 9> values{};
            std::memcpy(values.data(), cursor, sizeof(values));
            triangle.material = Read<std::uint32_t>(cursor + 36);
            if (triangle.material < 1 || triangle.material > 28 ||
                std::any_of(values.begin(), values.end(),
                            [](float value) { return !std::isfinite(value); }))
                throw std::runtime_error("Footstep triangle is invalid");
            std::copy_n(values.begin(), 3, triangle.origin.begin());
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                triangle.u[axis] = values[3 + axis] - values[axis];
                triangle.v[axis] = values[6 + axis] - values[axis];
            }
            triangle.determinant = triangle.u[0] * triangle.v[1] - triangle.u[1] * triangle.v[0];
            if (std::abs(triangle.determinant) < 0.001f)
                continue;

            const auto minimumX = (std::min)({values[0], values[3], values[6]});
            const auto maximumX = (std::max)({values[0], values[3], values[6]});
            const auto minimumY = (std::min)({values[1], values[4], values[7]});
            const auto maximumY = (std::max)({values[1], values[4], values[7]});
            const auto x0 = static_cast<int>(std::floor(minimumX / 128.0f));
            const auto x1 = static_cast<int>(std::floor(maximumX / 128.0f));
            const auto y0 = static_cast<int>(std::floor(minimumY / 128.0f));
            const auto y1 = static_cast<int>(std::floor(maximumY / 128.0f));
            const auto triangleIndex = triangles_.size();
            triangles_.push_back(triangle);
            if (std::int64_t(x1 - x0 + 1) * std::int64_t(y1 - y0 + 1) > 100000)
            {
                broadTriangles_.push_back(triangleIndex);
                continue;
            }
            for (int x = x0; x <= x1; ++x)
                for (int y = y0; y <= y1; ++y)
                    cells_[Key(x, y)].push_back(triangleIndex);
        }
    }

    std::uint32_t At(float x, float y, float z) const
    {
        const auto cell = cells_.find(Key(static_cast<int>(std::floor(x / 128.0f)),
                                          static_cast<int>(std::floor(y / 128.0f))));
        float best = 12.01f;
        std::uint32_t material = 5;
        auto consider = [&](const std::size_t index) {
            const auto &triangle = triangles_[index];
            const float dx = x - triangle.origin[0];
            const float dy = y - triangle.origin[1];
            const float s = (dx * triangle.v[1] - dy * triangle.v[0]) / triangle.determinant;
            const float t = (triangle.u[0] * dy - triangle.u[1] * dx) / triangle.determinant;
            if (s < -0.002f || t < -0.002f || s + t > 1.002f)
                return;
            const float distance =
                std::abs(z - triangle.origin[2] - s * triangle.u[2] - t * triangle.v[2]);
            if (distance < best)
            {
                best = distance;
                material = triangle.material;
            }
        };
        if (cell != cells_.end())
            for (const auto index : cell->second)
                consider(index);
        for (const auto index : broadTriangles_)
            consider(index);
        return material;
    }

  private:
    static std::uint64_t Key(int x, int y)
    {
        return (std::uint64_t(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y);
    }

    std::vector<FloorTriangle> triangles_;
    std::vector<std::size_t> broadTriangles_;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells_;
};

class ReplayHavok;
thread_local ReplayHavok *g_activeHavok = nullptr;

struct SaveContext
{
    ReplayHavok *havok = nullptr;
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> objects;
    std::map<std::pair<std::uintptr_t, std::uintptr_t>, int> indices;
    int nextId = 1;
    bool failed = false;
};
thread_local SaveContext *g_saveContext = nullptr;

class ReplayHavok
{
  public:
    explicit ReplayHavok(const std::filesystem::path &executable)
    {
        const auto file = ReadFile(executable);
        if (Sha256(file) != kReplaySha256)
            throw std::runtime_error(
                "Collision baking requires the original Replay 1.20.4.7623265 executable");
        if (file.size() < sizeof(IMAGE_DOS_HEADER))
            throw std::runtime_error("Replay executable is truncated");

        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(file.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            std::size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > file.size())
            throw std::runtime_error("Replay executable has an invalid PE header");
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(file.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            throw std::runtime_error("Replay executable is not PE32+");

        base_ = static_cast<std::uint8_t *>(VirtualAlloc(
            reinterpret_cast<void *>(nt->OptionalHeader.ImageBase), nt->OptionalHeader.SizeOfImage,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (reinterpret_cast<std::uintptr_t>(base_) != nt->OptionalHeader.ImageBase)
            throw std::runtime_error(
                "Replay's preferred address is unavailable; use a fresh conversion process");
        imageSize_ = nt->OptionalHeader.SizeOfImage;
        std::memcpy(base_, file.data(),
                    (std::min<std::size_t>)(nt->OptionalHeader.SizeOfHeaders, file.size()));

        const auto *sections = IMAGE_FIRST_SECTION(nt);
        for (std::uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto &section = sections[i];
            if (std::size_t(section.PointerToRawData) + section.SizeOfRawData > file.size() ||
                std::size_t(section.VirtualAddress) + section.SizeOfRawData > imageSize_)
                throw std::runtime_error("Replay section lies outside the image");
            std::memcpy(base_ + section.VirtualAddress, file.data() + section.PointerToRawData,
                        section.SizeOfRawData);
        }
        PatchKernelImports(nt);

        g_activeHavok = this;
        auto *heap = Allocate(128);
        auto *vtable = Allocate(128);
        Write(heap, reinterpret_cast<std::uintptr_t>(vtable));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 8,
              reinterpret_cast<std::uintptr_t>(&AllocateBlock));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 16,
              reinterpret_cast<std::uintptr_t>(&FreeBlock));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 24,
              reinterpret_cast<std::uintptr_t>(&AllocateBuffer));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 32,
              reinterpret_cast<std::uintptr_t>(&FreeBlock));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 40,
              reinterpret_cast<std::uintptr_t>(&ReallocateBuffer));
        Write(Address(0x5A09F68), reinterpret_cast<std::uintptr_t>(vtable));

        auto *router = Allocate(256);
        for (const auto offset : {0x50, 0x58, 0x60})
            Write(reinterpret_cast<std::uint8_t *>(router) + offset,
                  reinterpret_cast<std::uintptr_t>(heap));
        constexpr std::uint32_t stackSize = 16u * 1024u * 1024u;
        auto *stack = Allocate(stackSize);
        Write(reinterpret_cast<std::uint8_t *>(router) + 0x10, stackSize);
        Write(reinterpret_cast<std::uint8_t *>(router) + 0x18,
              reinterpret_cast<std::uintptr_t>(stack));
        Write(reinterpret_cast<std::uint8_t *>(router) + 0x20,
              reinterpret_cast<std::uintptr_t>(stack) + stackSize);
        Write(reinterpret_cast<std::uint8_t *>(router) + 0x28, std::uintptr_t{});
        router_ = router;
        Write(Address(0x12D42A38), reinterpret_cast<std::uintptr_t>(router));
        Write(Address(0x12D42A40), std::uint32_t{1000});
        Write(Address(0x23513B8), reinterpret_cast<std::uintptr_t>(&TlsValue));

        auto *crt = LoadLibraryW(L"ucrtbase.dll");
        const auto formatter =
            reinterpret_cast<std::uintptr_t>(GetProcAddress(crt, "__stdio_common_vsnprintf_s"));
        if (!formatter)
            throw std::runtime_error("ucrtbase formatter is unavailable");
        std::array<std::uint8_t, 12> thunk{0x48, 0xB8};
        std::memcpy(thunk.data() + 2, &formatter, sizeof(formatter));
        thunk[10] = 0xFF;
        thunk[11] = 0xE0;
        std::memcpy(Address(0x220F0D4), thunk.data(), thunk.size());

        Function<int(void *)>(0x1CCAC80)(Address(0x12D48EF8));
        const auto &codeSection = sections[0];
        const auto *code = file.data() + codeSection.PointerToRawData;
        const auto codeSize = std::size_t(codeSection.SizeOfRawData);
        const auto codeRva = std::uintptr_t(codeSection.VirtualAddress);

        std::size_t registrations = 0;
        for (std::size_t offset = 0; offset + 19 <= codeSize; ++offset)
        {
            if (code[offset] != 0x48 || code[offset + 1] != 0x8D || code[offset + 2] != 0x15 ||
                code[offset + 7] != 0x48 || code[offset + 8] != 0x8D || code[offset + 9] != 0x0D ||
                code[offset + 14] != 0xE9)
                continue;
            const auto target = codeRva + offset + 19 + Read<std::int32_t>(code + offset + 15);
            if (target == 0x1C98120)
            {
                Function<void()>(codeRva + offset)();
                ++registrations;
            }
        }
        if (registrations != 2564)
            throw std::runtime_error("Incomplete native reflection registration");
        Function<int(void *)>(0x1CE58F0)(Address(0x12D49C38));
        Function<int()>(0x1C98160)();

        std::size_t serializers = 0;
        for (std::size_t offset = 0; offset + 19 <= codeSize; ++offset)
        {
            if (code[offset] != 0x48 || code[offset + 1] != 0x8D || code[offset + 2] != 0x0D ||
                code[offset + 7] != 0xE8)
                continue;
            const auto object = codeRva + offset + 7 + Read<std::int32_t>(code + offset + 3);
            const auto constructor = codeRva + offset + 12 + Read<std::int32_t>(code + offset + 8);
            if ((constructor != 0x1C96C40 && constructor != 0x1C94350 && constructor != 0x1C94310 &&
                 constructor != 0x1C942D0) ||
                object < 0x29B1000)
                continue;
            Function<void(void *, void *)>(constructor)(Address(object), nullptr);
            if (code[offset + 12] == 0x48 && code[offset + 13] == 0x8D && code[offset + 14] == 0x05)
            {
                const auto vtableRva =
                    codeRva + offset + 19 + Read<std::int32_t>(code + offset + 15);
                Write(Address(object), reinterpret_cast<std::uintptr_t>(Address(vtableRva)));
            }
            ++serializers;
        }
        if (serializers != 83)
            throw std::runtime_error("Incomplete native serializer registration");
        Function<void()>(0x365890)();
    }

    ~ReplayHavok()
    {
        if (g_activeHavok == this)
            g_activeHavok = nullptr;
        for (const auto &[address, unused] : allocations_)
        {
            (void)unused;
            _aligned_free(address);
        }
        if (base_)
            VirtualFree(base_, 0, MEM_RELEASE);
    }

    ReplayHavok(const ReplayHavok &) = delete;
    ReplayHavok &operator=(const ReplayHavok &) = delete;

    void *Allocate(std::size_t bytes)
    {
        if (bytes == 0 || bytes > kMaximumAllocation)
            throw std::runtime_error("Havok allocation size is invalid");
        void *address = _aligned_malloc(bytes, 16);
        if (!address)
            throw std::bad_alloc();
        std::memset(address, 0, bytes);
        allocations_.emplace(address, bytes);
        return address;
    }

    void Release(void *address)
    {
        const auto allocation = allocations_.find(address);
        if (allocation == allocations_.end())
            return;
        _aligned_free(address);
        allocations_.erase(allocation);
    }

    template <typename Signature> auto Function(std::uintptr_t rva) const
    {
        return reinterpret_cast<Signature *>(base_ + rva);
    }

    void *Address(std::uintptr_t rva) const
    {
        return base_ + rva;
    }

    std::uintptr_t Base() const
    {
        return reinterpret_cast<std::uintptr_t>(base_);
    }

    std::vector<std::uint8_t> Save(void *root, std::size_t capacity,
                                   const std::uintptr_t typeRva = kShapeListType)
    {
        auto *writer = Allocate(32);
        Function<void *(void *, void *)>(0x1CB8B60)(writer, nullptr);
        auto *output = static_cast<std::uint8_t *>(Allocate(capacity));
        auto *config = static_cast<std::uint8_t *>(Allocate(32));
        auto *buffer = Allocate(64);
        Write(config + 8, reinterpret_cast<std::uintptr_t>(output));
        Write(config + 16, static_cast<std::uint64_t>(capacity));
        Function<void(void *)>(0x1C9A690)(buffer);
        Function<void(void *, void *)>(0x1C9AA50)(buffer, config);
        Function<void(void *, void *)>(0x1CB9B90)(writer, buffer);

        SaveContext context;
        context.havok = this;
        g_saveContext = &context;
        auto *provider = static_cast<std::uint8_t *>(Allocate(32));
        auto *vtable = static_cast<std::uint8_t *>(Allocate(32));
        Write(provider, reinterpret_cast<std::uintptr_t>(vtable));
        Write(vtable, reinterpret_cast<std::uintptr_t>(&ProviderIndex));
        Write(vtable + 8, reinterpret_cast<std::uintptr_t>(&ProviderReference));
        Write(vtable + 16, reinterpret_cast<std::uintptr_t>(&ProviderWritten));
        Write(vtable + 24, reinterpret_cast<std::uintptr_t>(&ProviderReserve));
        auto *typed = static_cast<std::uint8_t *>(Allocate(24));
        auto *status = static_cast<std::uint8_t *>(Allocate(16));
        Write(typed, reinterpret_cast<std::uintptr_t>(root));
        Write(typed + 8, Base() + typeRva);
        ProviderIndex(nullptr, typed);

        for (std::size_t i = 0; i < context.objects.size(); ++i)
        {
            Write(typed, context.objects[i].first);
            Write(typed + 8, context.objects[i].second);
            Write(typed + 16, std::uintptr_t{});
            Function<void *(void *, void *, void *, void *)>(0x1CBB310)(writer, status, typed,
                                                                        provider);
            if (context.failed || Read<int>(status) < 0)
            {
                g_saveContext = nullptr;
                throw std::runtime_error("Native collision serialization failed");
            }
        }
        Function<void(void *)>(0x1CBA7A0)(writer);
        g_saveContext = nullptr;

        const auto end = Read<std::uintptr_t>(buffer);
        const auto begin = reinterpret_cast<std::uintptr_t>(output);
        if (end <= begin || end - begin > capacity)
            throw std::runtime_error("Native collision output exceeded its buffer");
        return {output, output + (end - begin)};
    }

    void *Load(const std::vector<std::uint8_t> &bytes,
               const std::uintptr_t typeRva = kShapeListType)
    {
        auto *buffer = Allocate(bytes.size());
        std::memcpy(buffer, bytes.data(), bytes.size());
        auto *context = Allocate(32);
        auto *result = Allocate(24);
        Function<void *(void *)>(0x1C9B480)(context);
        Function<void *(void *, void *, void *, std::size_t, void *)>(0x1C9CF00)(
            context, result, buffer, bytes.size(), Address(typeRva));
        auto *root = reinterpret_cast<void *>(Read<std::uintptr_t>(result));
        if (!root)
            throw std::runtime_error("Replay rejected the serialized collision");
        return root;
    }

  private:
    static void *AllocateBlock(void *, int bytes)
    {
        try
        {
            return g_activeHavok->Allocate(bytes);
        }
        catch (...)
        {
            return nullptr;
        }
    }

    static void FreeBlock(void *, void *address, int)
    {
        if (g_activeHavok)
            g_activeHavok->Release(address);
    }

    static void *AllocateBuffer(void *, void *sizeAddress)
    {
        try
        {
            return g_activeHavok->Allocate(Read<int>(sizeAddress));
        }
        catch (...)
        {
            return nullptr;
        }
    }

    static void *ReallocateBuffer(void *, void *oldAddress, int oldSize, void *sizeAddress)
    {
        try
        {
            const auto bytes = Read<int>(sizeAddress);
            auto *replacement = g_activeHavok->Allocate(bytes);
            if (oldAddress)
            {
                std::memcpy(replacement, oldAddress, (std::min)(bytes, oldSize));
                g_activeHavok->Release(oldAddress);
            }
            return replacement;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    static void *WINAPI TlsValue(DWORD index)
    {
        return g_activeHavok && index == 1000 ? g_activeHavok->router_ : nullptr;
    }

    static int ProviderReserve(void *)
    {
        return g_saveContext ? g_saveContext->nextId++ : 0;
    }

    static int ProviderIndex(void *, void *object)
    {
        if (!g_saveContext)
            return 0;
        const auto address = Read<std::uintptr_t>(object);
        const auto type = Read<std::uintptr_t>(static_cast<std::uint8_t *>(object) + 8);
        if (!address)
            return 0;
        const auto key = std::make_pair(address, type);
        if (const auto found = g_saveContext->indices.find(key);
            found != g_saveContext->indices.end())
            return found->second;
        const auto id = ProviderReserve(nullptr);
        g_saveContext->indices.emplace(key, id);
        g_saveContext->objects.push_back(key);
        return id;
    }

    static int ProviderReference(void *, void *object)
    {
        if (!g_saveContext)
            return 0;
        try
        {
            const auto address = Read<std::uintptr_t>(object);
            const auto target =
                address ? Read<std::uintptr_t>(reinterpret_cast<void *>(address)) : 0;
            if (!target)
                return 0;
            auto *typed = static_cast<std::uint8_t *>(g_saveContext->havok->Allocate(24));
            const auto vtable = Read<std::uintptr_t>(reinterpret_cast<void *>(target));
            const auto method = Read<std::uintptr_t>(reinterpret_cast<void *>(vtable));
            g_saveContext->havok->Function<void *(void *, void *)>(
                method - g_saveContext->havok->Base())(reinterpret_cast<void *>(target), typed);
            const auto result = ProviderIndex(nullptr, typed);
            g_saveContext->havok->Release(typed);
            return result;
        }
        catch (...)
        {
            g_saveContext->failed = true;
            return 0;
        }
    }

    static void ProviderWritten(void *, int) {}

    void PatchKernelImports(const IMAGE_NT_HEADERS64 *nt)
    {
        const auto &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress || !directory.Size)
            throw std::runtime_error("Replay executable has no imports");
        auto *descriptor =
            reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base_ + directory.VirtualAddress);
        for (; descriptor->Name; ++descriptor)
        {
            const auto *library = reinterpret_cast<const char *>(base_ + descriptor->Name);
            if (_stricmp(library, "kernel32.dll") != 0)
                continue;
            auto *names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(
                base_ + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk
                                                        : descriptor->FirstThunk));
            auto *addresses =
                reinterpret_cast<IMAGE_THUNK_DATA64 *>(base_ + descriptor->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++addresses)
            {
                FARPROC function = nullptr;
                if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
                {
                    function = GetProcAddress(
                        GetModuleHandleW(L"kernel32.dll"),
                        reinterpret_cast<const char *>(IMAGE_ORDINAL64(names->u1.Ordinal)));
                }
                else
                {
                    const auto *import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(
                        base_ + names->u1.AddressOfData);
                    function = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), import->Name);
                }
                if (!function)
                    throw std::runtime_error("Replay kernel32 import is unavailable");
                addresses->u1.Function = reinterpret_cast<ULONGLONG>(function);
            }
        }
    }

    std::uint8_t *base_ = nullptr;
    std::size_t imageSize_ = 0;
    void *router_ = nullptr;
    std::unordered_map<void *, std::size_t> allocations_;
};

template <typename T>
void WriteArray(ReplayHavok &havok, std::uint8_t *root, std::size_t offset,
                const std::vector<T> &values)
{
    if (values.empty())
    {
        Write(root + offset, std::uintptr_t{});
        Write(root + offset + 8, std::uint32_t{});
        Write(root + offset + 12, std::uint32_t{});
        return;
    }
    auto *memory = static_cast<std::uint8_t *>(havok.Allocate(sizeof(T) * values.size()));
    std::memcpy(memory, values.data(), sizeof(T) * values.size());
    Write(root + offset, reinterpret_cast<std::uintptr_t>(memory));
    Write(root + offset + 8, static_cast<std::uint32_t>(values.size()));
    Write(root + offset + 12, static_cast<std::uint32_t>(values.size()) | 0x80000000u);
}

#pragma pack(push, 1)
struct ShapeTag
{
    std::uint32_t contents;
    std::uint32_t hash;
    std::uint16_t collisionFilterInfo;
    std::uint8_t padding[6]{};
    std::uint64_t userData;
};
#pragma pack(pop)
static_assert(sizeof(ShapeTag) == 24);

#pragma pack(push, 1)
struct NativeMeshTriangle
{
    std::array<std::uint32_t, 3> indices{};
    std::uint16_t shapeTag{};
    std::uint16_t padding{};
};
#pragma pack(pop)
static_assert(sizeof(NativeMeshTriangle) == 16);

void *BuildCompressedMesh(ReplayHavok &havok, const Mesh &mesh,
                          const std::vector<std::uint16_t> &shapeTags)
{
    if (mesh.vertices.size() > std::numeric_limits<std::int32_t>::max() ||
        mesh.triangles.size() > std::numeric_limits<std::int32_t>::max() ||
        mesh.triangles.size() != shapeTags.size())
        throw std::runtime_error("Native compressed mesh dimensions are invalid");

    std::vector<std::array<float, 4>> vertices(mesh.vertices.size());
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
        for (std::size_t axis = 0; axis < 3; ++axis)
            vertices[index][axis] = mesh.vertices[index][axis] / 32.0f;
    std::vector<NativeMeshTriangle> triangles(mesh.triangles.size());
    for (std::size_t index = 0; index < mesh.triangles.size(); ++index)
    {
        triangles[index].indices = mesh.triangles[index].indices;
        triangles[index].shapeTag = shapeTags[index];
    }

    auto *vertexMemory =
        static_cast<std::uint8_t *>(havok.Allocate(vertices.size() * sizeof(vertices.front())));
    std::memcpy(vertexMemory, vertices.data(), vertices.size() * sizeof(vertices.front()));
    auto *triangleMemory =
        static_cast<std::uint8_t *>(havok.Allocate(triangles.size() * sizeof(triangles.front())));
    std::memcpy(triangleMemory, triangles.data(), triangles.size() * sizeof(triangles.front()));

    // hknpDefaultCompressedMeshShapeCinfo reads this hkGeometry-compatible view through its
    // native virtual methods. The backing arrays are marked external so Havok never owns them.
    auto *geometry = static_cast<std::uint8_t *>(havok.Allocate(56));
    Write(geometry + 24, reinterpret_cast<std::uintptr_t>(vertexMemory));
    Write(geometry + 32, static_cast<std::uint32_t>(vertices.size()));
    Write(geometry + 36, static_cast<std::uint32_t>(vertices.size()) | 0x80000000u);
    Write(geometry + 40, reinterpret_cast<std::uintptr_t>(triangleMemory));
    Write(geometry + 48, static_cast<std::uint32_t>(triangles.size()));
    Write(geometry + 52, static_cast<std::uint32_t>(triangles.size()) | 0x80000000u);

    auto *cinfo = static_cast<std::uint8_t *>(havok.Allocate(104));
    havok.Function<void *(void *)>(0x1F51F50)(cinfo);
    Write(cinfo, reinterpret_cast<std::uintptr_t>(havok.Address(0x2675BC0)));
    Write(cinfo + 72, reinterpret_cast<std::uintptr_t>(geometry));
    cinfo[80] = 1;

    auto *shape = static_cast<std::uint8_t *>(havok.Allocate(128));
    const auto built = havok.Function<void *(void *, const void *)>(0x1F12600)(shape, cinfo);
    if (built != shape)
        throw std::runtime_error("Replay rejected the native compressed mesh");
    return shape;
}

thread_local std::unique_ptr<ReplayHavok> g_preparedHavok;

} // namespace

void PrepareCollisionBaker(const std::filesystem::path &replayExecutable)
{
    if (!g_preparedHavok)
        g_preparedHavok = std::make_unique<ReplayHavok>(replayExecutable);
}

bool FindOpaqueString(const std::string_view value, std::uint32_t &id)
{
    return FindReplayOpaqueString(value, id);
}

struct ShapeGroup
{
    std::string name;
    std::vector<std::size_t> hulls;
    std::vector<std::size_t> meshes;
};

struct NativeShapeCheck
{
    void *shape{};
    std::size_t localIndex{};
    std::array<float, 6> bounds{};
};

std::vector<std::uint8_t> BuildShapeList(ReplayHavok &havok, const std::vector<Hull> &hulls,
                                         const std::vector<Mesh> &meshes,
                                         const std::vector<ShapeGroup> &groups,
                                         const FloorMaterials &floors, const bool useFloorMaterials,
                                         const std::vector<ShapeTag> &seedTags = {},
                                         std::vector<ShapeTag> *builtTags = nullptr)
{
    auto *config = static_cast<std::uint8_t *>(havok.Allocate(96));
    havok.Function<void *(void *)>(0x1E81300)(config);
    Write(config, 0.0f);
    config[4] = 0;
    config[16] = 1;
    config[17] = 0;

    auto *vertices = static_cast<std::uint8_t *>(havok.Allocate(252 * 16));
    auto *vertexArray = static_cast<std::uint8_t *>(havok.Allocate(16));
    // MapEnts becomes the process-wide Havok shape-tag codec when it is
    // activated.  Its tag indices must therefore start with the world tags;
    // otherwise a world shape's numeric tag resolves to an entity tag (or to
    // an empty table for maps without brush-model collision).
    std::vector<ShapeTag> tags = seedTags;
    using ShapeTagKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t,
                                   std::uint16_t>;
    std::map<ShapeTagKey, std::uint16_t> tagIndices;
    for (std::size_t index = 0; index < seedTags.size(); ++index)
    {
        const auto &tag = seedTags[index];
        const auto material = static_cast<std::uint32_t>((tag.userData >> 19) & 0x1FFFu);
        const auto surfaceFlags = static_cast<std::uint32_t>(tag.userData & 0x7FFFFu);
        const auto glassId = static_cast<std::uint16_t>(tag.userData >> 32);
        tagIndices.emplace(ShapeTagKey{tag.contents, material, surfaceFlags, glassId},
                           static_cast<std::uint16_t>(index));
    }
    const auto findTag = [&](const std::uint32_t shapeContents, const std::uint32_t material,
                             const std::uint32_t surfaceFlags,
                             const std::uint16_t glassId = 0) {
        if (material > 0x1FFFu || (glassId && !(shapeContents & 0x10u)))
            throw std::runtime_error("Native glass collision tag is invalid");
        const ShapeTagKey key{shapeContents, material, surfaceFlags, glassId};
        auto tag = tagIndices.find(key);
        if (tag != tagIndices.end())
            return tag->second;
        if (tags.size() >= std::numeric_limits<std::uint16_t>::max())
            throw std::runtime_error("Native collision has too many shape tags");
        const auto newIndex = static_cast<std::uint16_t>(tags.size());
        const std::uint64_t userData = (std::uint64_t((shapeContents & 1) ? 1 : 3) << 48) |
                                       (std::uint64_t(glassId) << 32) |
                                       (std::uint64_t(material) << 19) | surfaceFlags;
        tags.push_back({shapeContents, 0x1AB7BC33u, 0xFFFFu, {}, userData});
        tagIndices.emplace(key, newIndex);
        return newIndex;
    };
    std::vector<std::uintptr_t> compounds;
    std::vector<std::uintptr_t> names;
    std::vector<std::uint32_t> vertexCounts;
    std::vector<std::uint32_t> triangleCounts;
    std::vector<std::array<float, 4>> bounds;
    std::vector<std::uint32_t> contents;
    std::vector<std::uint32_t> shapeCounts;
    std::vector<std::vector<NativeShapeCheck>> checks;
    std::size_t builtHulls = 0;
    std::size_t builtMeshTriangles = 0;

    for (const auto &group : groups)
    {
        const std::size_t childCount = group.hulls.size() + group.meshes.size();
        if (childCount == 0 || childCount > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Native collision shape group is empty");
        auto *instances = static_cast<std::uint8_t *>(havok.Allocate(childCount * 112));
        std::array<float, 3> minimum{std::numeric_limits<float>::infinity(),
                                     std::numeric_limits<float>::infinity(),
                                     std::numeric_limits<float>::infinity()};
        std::array<float, 3> maximum{-std::numeric_limits<float>::infinity(),
                                     -std::numeric_limits<float>::infinity(),
                                     -std::numeric_limits<float>::infinity()};
        std::uint32_t allContents = 0;
        std::uint32_t totalVertices = 0;
        std::uint32_t totalTriangles = 0;
        std::vector<NativeShapeCheck> groupChecks;
        groupChecks.reserve(childCount);

        for (std::size_t localIndex = 0; localIndex < group.hulls.size(); ++localIndex)
        {
            const auto sourceIndex = group.hulls[localIndex];
            if (sourceIndex >= hulls.size())
                throw std::runtime_error("Native collision shape group has an invalid hull");
            const auto &hull = hulls[sourceIndex];
            std::array<float, 3> center{};
            for (const auto &point : hull.points)
                for (std::size_t axis = 0; axis < 3; ++axis)
                    center[axis] += point[axis];
            for (auto &value : center)
                value /= static_cast<float>(hull.points.size());
            const auto highest = std::max_element(
                hull.points.begin(), hull.points.end(),
                [](const auto &left, const auto &right) { return left[2] < right[2]; });
            // Surface type 9 maps to Replay's native breakable-glass flags
            // (9 << 19 == 0x480000); glassId is one-based in userData[32:47].
            const auto material = hull.glassId ? 9u :
                useFloorMaterials ? floors.At(center[0], center[1], (*highest)[2]) : 5u;
            const auto tag = findTag(hull.contents, material, hull.surfaceFlags, hull.glassId);
            allContents |= hull.contents;

            std::array<float, 6> shapeBounds{};

            for (std::size_t vertex = 0; vertex < hull.points.size(); ++vertex)
            {
                std::array<float, 4> converted{};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    converted[axis] = (hull.points[vertex][axis] - center[axis]) / 32.0f;
                    minimum[axis] = (std::min)(minimum[axis], hull.points[vertex][axis] / 32.0f);
                    maximum[axis] = (std::max)(maximum[axis], hull.points[vertex][axis] / 32.0f);
                    if (vertex == 0)
                        shapeBounds[axis] = shapeBounds[axis + 3] =
                            hull.points[vertex][axis] / 32.0f;
                    else
                    {
                        shapeBounds[axis] =
                            (std::min)(shapeBounds[axis], hull.points[vertex][axis] / 32.0f);
                        shapeBounds[axis + 3] =
                            (std::max)(shapeBounds[axis + 3], hull.points[vertex][axis] / 32.0f);
                    }
                }
                std::memcpy(vertices + vertex * 16, converted.data(), 16);
            }
            Write(vertexArray, reinterpret_cast<std::uintptr_t>(vertices));
            Write(vertexArray + 8, static_cast<std::uint32_t>(hull.points.size()));
            Write(vertexArray + 12, std::uint32_t{16});
            auto *shape =
                havok.Function<void *(void *, float, void *)>(0x1E82060)(vertexArray, 0.0f, config);
            if (!shape)
                throw std::runtime_error("Native convex construction failed at hull " +
                                         std::to_string(sourceIndex));
            totalVertices += static_cast<std::uint32_t>(hull.points.size());
            totalTriangles += static_cast<std::uint32_t>(hull.points.size() * 2 - 4);

            auto *instance = instances + localIndex * 112;
            const std::array<float, 16> transform{1,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  1,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  1,
                                                  0,
                                                  center[0] / 32.0f,
                                                  center[1] / 32.0f,
                                                  center[2] / 32.0f,
                                                  1};
            std::memcpy(instance, transform.data(), sizeof(transform));
            Write(instance + 12, std::uint32_t{0x3F000040});
            const std::array<float, 4> scale{1, 1, 1, 1};
            std::memcpy(instance + 64, scale.data(), sizeof(scale));
            Write(instance + 80, reinterpret_cast<std::uintptr_t>(shape));
            Write(instance + 88, tag);
            Write(instance + 90, std::uint16_t{0xFFFF});
            Write(instance + 100, std::uint16_t{0xFFFF});
            groupChecks.push_back({shape, localIndex, shapeBounds});
            if (++builtHulls % 1000 == 0)
                zt::info("collision: built %zu hulls", builtHulls);
        }

        for (std::size_t meshIndex = 0; meshIndex < group.meshes.size(); ++meshIndex)
        {
            const auto sourceIndex = group.meshes[meshIndex];
            if (sourceIndex >= meshes.size())
                throw std::runtime_error("Native collision shape group has an invalid mesh");
            const auto &mesh = meshes[sourceIndex];
            std::vector<std::uint16_t> triangleTags;
            triangleTags.reserve(mesh.triangles.size());
            for (const auto &triangle : mesh.triangles)
            {
                triangleTags.push_back(
                    findTag(triangle.contents, triangle.material, triangle.surfaceFlags));
                allContents |= triangle.contents;
            }
            auto *shape = BuildCompressedMesh(havok, mesh, triangleTags);

            std::array<float, 6> shapeBounds{};
            for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex)
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const float value = mesh.vertices[vertex][axis] / 32.0f;
                    minimum[axis] = (std::min)(minimum[axis], value);
                    maximum[axis] = (std::max)(maximum[axis], value);
                    if (vertex == 0)
                        shapeBounds[axis] = shapeBounds[axis + 3] = value;
                    else
                    {
                        shapeBounds[axis] = (std::min)(shapeBounds[axis], value);
                        shapeBounds[axis + 3] = (std::max)(shapeBounds[axis + 3], value);
                    }
                }
            totalVertices += static_cast<std::uint32_t>(mesh.vertices.size());
            totalTriangles += static_cast<std::uint32_t>(mesh.triangles.size());
            builtMeshTriangles += mesh.triangles.size();

            const std::size_t localIndex = group.hulls.size() + meshIndex;
            auto *instance = instances + localIndex * 112;
            const std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            std::memcpy(instance, transform.data(), sizeof(transform));
            Write(instance + 12, std::uint32_t{0x3F000040});
            const std::array<float, 4> scale{1, 1, 1, 1};
            std::memcpy(instance + 64, scale.data(), sizeof(scale));
            Write(instance + 80, reinterpret_cast<std::uintptr_t>(shape));
            Write(instance + 88, std::uint16_t{0xFFFF});
            Write(instance + 90, std::uint16_t{0xFFFF});
            Write(instance + 100, std::uint16_t{0xFFFF});
            groupChecks.push_back({shape, localIndex, shapeBounds});
        }

        auto *array = static_cast<std::uint8_t *>(havok.Allocate(16));
        Write(array, reinterpret_cast<std::uintptr_t>(instances));
        Write(array + 8, static_cast<std::uint32_t>(childCount));
        Write(array + 12, static_cast<std::uint32_t>(childCount) | 0x80000000u);
        auto *compound = havok.Function<void *(void *)>(0x161C770)(array);
        if (!compound)
            throw std::runtime_error("Native compound construction failed");

        auto *name = static_cast<char *>(havok.Allocate(group.name.size() + 1));
        std::memcpy(name, group.name.c_str(), group.name.size() + 1);
        compounds.push_back(reinterpret_cast<std::uintptr_t>(compound));
        names.push_back(reinterpret_cast<std::uintptr_t>(name));
        vertexCounts.push_back(totalVertices);
        triangleCounts.push_back(totalTriangles);
        bounds.push_back({minimum[0], minimum[1], minimum[2], 0});
        bounds.push_back({maximum[0], maximum[1], maximum[2], 0});
        contents.push_back(allContents);
        shapeCounts.push_back(static_cast<std::uint32_t>(childCount));
        checks.push_back(std::move(groupChecks));
    }

    // Replay links MapEnts through HavokPhysics_AddShapeList unconditionally.
    // A map without brush-model collision therefore still needs a serialized
    // empty HavokPhysicsShapeList; a null byte stream faults during linking.
    auto *root = static_cast<std::uint8_t *>(havok.Allocate(152));
    std::memset(root, 0, 152);
    WriteArray(havok, root, 0, compounds);
    WriteArray(havok, root, 16, std::vector<std::int32_t>(groups.size(), -1));
    WriteArray(havok, root, 32, names);
    WriteArray(havok, root, 48, vertexCounts);
    WriteArray(havok, root, 64, triangleCounts);
    WriteArray(havok, root, 80, bounds);
    WriteArray(havok, root, 104, tags);
    WriteArray(havok, root, 120, contents);
    WriteArray(havok, root, 136, shapeCounts);

    const std::size_t estimatedCapacity =
        1024u * 1024u + builtHulls * 8192u + builtMeshTriangles * 256u;
    const auto capacity = (std::min)(kMaximumAllocation, estimatedCapacity);
    auto output = havok.Save(root, capacity);
    auto *loaded = static_cast<std::uint8_t *>(havok.Load(output));
    if (Read<std::uint32_t>(loaded + 8) != groups.size() ||
        Read<std::uint32_t>(loaded + 112) != tags.size())
        throw std::runtime_error("Native collision round trip lost shapes or tags");
    if (!tags.empty() && std::memcmp(reinterpret_cast<void *>(Read<std::uintptr_t>(loaded + 104)),
                                     reinterpret_cast<void *>(Read<std::uintptr_t>(root + 104)),
                                     tags.size() * sizeof(ShapeTag)) != 0)
        throw std::runtime_error("Native collision round trip changed shape tags");

    const auto restoredCompounds = Read<std::uintptr_t>(loaded);
    auto *queryBounds = havok.Allocate(32);
    for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
    {
        const auto restored = Read<std::uintptr_t>(
            reinterpret_cast<void *>(restoredCompounds + groupIndex * sizeof(std::uintptr_t)));
        if (!restored ||
            Read<std::uint32_t>(reinterpret_cast<void *>(restored + 80)) != shapeCounts[groupIndex])
            throw std::runtime_error("Native collision round trip lost a compound");
        const auto restoredChildren = Read<std::uintptr_t>(reinterpret_cast<void *>(restored + 72));
        const auto originalCompound = compounds[groupIndex];
        const auto originalChildren =
            Read<std::uintptr_t>(reinterpret_cast<void *>(originalCompound + 72));
        for (const auto &check : checks[groupIndex])
        {
            const auto child = Read<std::uintptr_t>(
                reinterpret_cast<void *>(restoredChildren + check.localIndex * 112 + 80));
            if (!child ||
                std::memcmp(reinterpret_cast<void *>(child + 24),
                            static_cast<std::uint8_t *>(check.shape) + 24, 24) != 0 ||
                std::memcmp(reinterpret_cast<void *>(restoredChildren + check.localIndex * 112),
                            reinterpret_cast<void *>(originalChildren + check.localIndex * 112),
                            80) != 0)
                throw std::runtime_error("Native collision round trip changed shape " +
                                         std::to_string(check.localIndex));
            const auto vtable = Read<std::uintptr_t>(reinterpret_cast<void *>(child));
            const auto method = Read<std::uintptr_t>(reinterpret_cast<void *>(vtable + 32));
            havok.Function<void(void *, void *, void *)>(method - havok.Base())(
                reinterpret_cast<void *>(child),
                reinterpret_cast<void *>(restoredChildren + check.localIndex * 112), queryBounds);
            const auto *actual = static_cast<const float *>(queryBounds);
            const std::array<float, 6> measured{actual[0], actual[1], actual[2],
                                                actual[4], actual[5], actual[6]};
            for (std::size_t axis = 0; axis < measured.size(); ++axis)
                if (!std::isfinite(measured[axis]) ||
                    std::abs(measured[axis] - check.bounds[axis]) > 0.05f)
                    throw std::runtime_error("Native bounds query disagrees at shape " +
                                             std::to_string(check.localIndex));
        }
    }
    zt::info("collision: baked %zu hulls and %zu mesh triangles in %zu shape lists and %zu tags "
             "into %zu bytes",
             builtHulls, builtMeshTriangles, groups.size(), tags.size(), output.size());
    if (builtTags)
        *builtTags = tags;
    return output;
}

BakeResult BakeCollision(const BakeInput &input)
{
    std::unique_ptr<ReplayHavok> localHavok;
    if (!g_preparedHavok)
        localHavok = std::make_unique<ReplayHavok>(input.replayExecutable);
    auto &havok = g_preparedHavok ? *g_preparedHavok : *localHavok;
    const auto collision = ReadCollision(input.collision);
    const FloorMaterials floors(input.footsteps);

    BakeResult result;
    result.models = collision.models;
    result.hulls.reserve(collision.hulls.size());
    for (const Hull &source : collision.hulls)
    {
        CollisionHull hull;
        hull.minimum.fill(std::numeric_limits<float>::infinity());
        hull.maximum.fill(-std::numeric_limits<float>::infinity());
        for (const auto &point : source.points)
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                hull.minimum[axis] = (std::min)(hull.minimum[axis], point[axis]);
                hull.maximum[axis] = (std::max)(hull.maximum[axis], point[axis]);
            }
        hull.contents = source.contents;
        hull.model = source.model;
        hull.slabs = source.slabs;
        for (const auto &plane : source.ladderPlanes)
        {
            if (result.ladders.size() >= 512)
                throw std::runtime_error("Collision has more than 512 ladder faces");

            LadderFace ladder;
            for (std::size_t axis = 0; axis < 3; ++axis)
                ladder.bottom[axis] = (hull.minimum[axis] + hull.maximum[axis]) * 0.5f;
            const float distance =
                plane[3] - (plane[0] * ladder.bottom[0] + plane[1] * ladder.bottom[1] +
                            plane[2] * ladder.bottom[2]);
            ladder.bottom[0] += plane[0] * distance;
            ladder.bottom[1] += plane[1] * distance;
            ladder.bottom[2] = hull.minimum[2];
            ladder.top = ladder.bottom;
            ladder.top[2] += std::floor((hull.maximum[2] - hull.minimum[2]) / 12.0f) * 12.0f;
            std::copy_n(plane.data(), 3, ladder.normal.data());
            const float tangentX = -plane[1];
            const float tangentY = plane[0];
            ladder.width = std::abs(tangentX) * (hull.maximum[0] - hull.minimum[0]) +
                           std::abs(tangentY) * (hull.maximum[1] - hull.minimum[1]);
            if (ladder.top[2] - ladder.bottom[2] >= 48.0f && ladder.width >= 12.0f)
                result.ladders.push_back(ladder);
        }
        result.hulls.push_back(std::move(hull));
    }
    ShapeGroup world{"World Entity Main_Full"};
    std::vector<std::vector<std::size_t>> modelHulls(result.models.size());
    std::vector<std::vector<std::size_t>> modelMeshes(result.models.size());
    for (std::size_t index = 0; index < collision.hulls.size(); ++index)
    {
        const auto model = collision.hulls[index].model;
        modelHulls[model].push_back(index);
        if (model == 0)
            world.hulls.push_back(index);
    }
    for (std::size_t index = 0; index < collision.meshes.size(); ++index)
    {
        const auto model = collision.meshes[index].model;
        modelMeshes[model].push_back(index);
        if (model == 0)
            world.meshes.push_back(index);
    }

    std::vector<ShapeGroup> entities;
    for (std::size_t model = 1; model < modelHulls.size(); ++model)
    {
        if (modelHulls[model].empty() && modelMeshes[model].empty())
            continue;
        if (entities.size() >= std::numeric_limits<std::uint16_t>::max())
            throw std::runtime_error("Collision has too many brush-model shapes");
        result.models[model].shapeIndex = static_cast<std::uint16_t>(entities.size());
        entities.push_back({"Brush Model " + std::to_string(model), std::move(modelHulls[model]),
                            std::move(modelMeshes[model])});
    }

    std::vector<ShapeTag> worldTags;
    result.world = BuildShapeList(havok, collision.hulls, collision.meshes, {std::move(world)},
                                  floors, true, {}, &worldTags);
    result.entities = BuildShapeList(havok, collision.hulls, collision.meshes, entities, floors,
                                     false, worldTags);
    zt::info("collision: retained %zu source brush models (%zu collision shapes)",
             result.models.size(), entities.size());
    if (!result.ladders.empty())
        zt::info("collision: retained %zu native ladder faces", result.ladders.size());
    return result;
}

PhysicsAsset BakePhysicsAsset(std::string name, const PhysicsMesh &source,
                               const std::uint32_t useCategory,
                               const std::uint32_t simulationCategory)
{
    if (!g_preparedHavok)
        throw std::runtime_error("PrepareCollisionBaker must be called before model physics");
    if (name.empty() || source.vertices.size() < 3 || source.triangles.empty() ||
        source.vertices.size() > std::numeric_limits<std::int32_t>::max() ||
        source.triangles.size() > std::numeric_limits<std::int32_t>::max() || !source.contents ||
        !((useCategory == 3 && simulationCategory == 1) ||
          (useCategory == 7 && simulationCategory == 9)))
        throw std::runtime_error("Native model physics input is invalid");

    Mesh mesh;
    mesh.vertices = source.vertices;
    mesh.triangles.reserve(source.triangles.size());
    for (const auto &triangle : source.triangles)
    {
        for (const auto index : triangle)
            if (index >= mesh.vertices.size())
                throw std::runtime_error("Native model physics triangle has an invalid vertex");
        const auto &a = mesh.vertices[triangle[0]];
        const auto &b = mesh.vertices[triangle[1]];
        const auto &c = mesh.vertices[triangle[2]];
        for (const auto *vertex : {&a, &b, &c})
            for (const float value : *vertex)
                if (!std::isfinite(value) || std::abs(value) > 1000000.0f)
                    throw std::runtime_error("Native model physics vertex exceeds Replay bounds");
        const std::array<float, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const std::array<float, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const std::array<float, 3> cross{ab[1] * ac[2] - ab[2] * ac[1],
                                         ab[2] * ac[0] - ab[0] * ac[2],
                                         ab[0] * ac[1] - ab[1] * ac[0]};
        if (cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2] < 1.0e-12f)
            continue;
        mesh.triangles.push_back({triangle, source.contents, 0, 5});
    }
    if (mesh.triangles.empty())
        throw std::runtime_error("Native model physics has no non-degenerate triangles");

    auto &havok = *g_preparedHavok;
    auto *shape =
        BuildCompressedMesh(havok, mesh, std::vector<std::uint16_t>(mesh.triangles.size(), 0));
    auto setArray = [&](std::uint8_t *field, void *data, const std::uint32_t count) {
        Write(field, reinterpret_cast<std::uintptr_t>(data));
        Write(field + 8, count);
        Write(field + 12, count | 0x80000000u);
    };

    auto *root = static_cast<std::uint8_t *>(havok.Allocate(224));
    havok.Function<void(void *, void *, int)>(0x1654670)(root, nullptr, 1);
    Write(root, static_cast<std::int32_t>(useCategory));

    // HavokPhysicsAsset carries a second copy of the per-body policy used by the native
    // instantiator.  These arrays are part of the reflected tagfile root; the outer
    // PhysicsAsset simulationCategories/bodyContents arrays do not populate them.  Replay
    // indexes every one-body DynEnt asset's server-usage entry without a null check.
    //
    // The CRCs and cardinalities below match shipped Replay 1.20 one-body assets.
    // DynEnt models use simulation category 1; the glass dummy uses custom category 9.
    // The two event name arrays contain one empty hkStringPtr, as shipped assets do.
    const bool glass = useCategory == 7 && simulationCategory == 9;
    const std::uint32_t bodyQualityNameCrc = glass ? 0x98996C20u : 0x7923E35Cu;
    const std::uint32_t materialNameCrc = glass ? 0xF728E572u : 0x4B02BC9Du;
    const std::uint32_t motionPropertiesNameCrc = glass ? 0x8B8AA36Fu : 0xE9D0CCAFu;
    constexpr std::uint32_t kServerUsageDefault = 0;
    constexpr std::uint32_t kBodyDriverNone = 0;

    auto *emptyEventName = static_cast<char *>(havok.Allocate(1));
    const auto emptyString = reinterpret_cast<std::uintptr_t>(emptyEventName);
    WriteArray(havok, root, 0x10, std::vector<std::uint32_t>{bodyQualityNameCrc});
    WriteArray(havok, root, 0x20, std::vector<std::uint32_t>{materialNameCrc});
    WriteArray(havok, root, 0x30, std::vector<std::uint32_t>{motionPropertiesNameCrc});
    WriteArray(havok, root, 0x40, std::vector<std::uintptr_t>{emptyString});
    WriteArray(havok, root, 0x50, std::vector<std::uintptr_t>{emptyString});
    WriteArray(havok, root, 0x60, std::vector<std::uint32_t>{kServerUsageDefault});
    WriteArray<std::uint32_t>(havok, root, 0x70, {}); // no constraints
    WriteArray(havok, root, 0x80, std::vector<std::uint32_t>{kBodyDriverNone});
    WriteArray(havok, root, 0x90, std::vector<std::uint32_t>{simulationCategory});

    auto *system = static_cast<std::uint8_t *>(havok.Allocate(104));
    havok.Function<void(void *, void *, int)>(0x1E617C0)(system, nullptr, 1);
    Write(root + 8, reinterpret_cast<std::uintptr_t>(system));

    auto *material = static_cast<std::uint8_t *>(havok.Allocate(112));
    havok.Function<void(void *, void *, int)>(0x1E70BB0)(material, nullptr, 1);
    setArray(system + 24, material, 1);

    auto *motion = static_cast<std::uint8_t *>(havok.Allocate(112));
    havok.Function<void(void *, void *, int)>(0x1E5D790)(motion, nullptr, 1);
    setArray(system + 40, motion, 1);

    auto *body = static_cast<std::uint8_t *>(havok.Allocate(192));
    havok.Function<void(void *, void *, int)>(0x1E64350)(body, nullptr, 1);
    Write(body, reinterpret_cast<std::uintptr_t>(shape));
    // Replay ray-hit conversion interns HavokPhysics_GetRigidBodyName without
    // a null check. Preserve a native body name, including on falling glass.
    const std::string_view bodyName = glass ? "fxglassdummy" : "tag_origin";
    auto *bodyNameData = static_cast<char *>(havok.Allocate(bodyName.size() + 1));
    std::memcpy(bodyNameData, bodyName.data(), bodyName.size());
    Write(body + 0x18, reinterpret_cast<std::uintptr_t>(bodyNameData));
    Write(body + 0x14, std::uint16_t{0});
    Write(body + 0x16, std::uint8_t{0});
    Write(body + 0x88, std::uint16_t{0});
    if (glass)
    {
        // FxGlass creates a dynamic body from this asset at break time.  The
        // shipped glasschunkdummydefault keeps the body collision filter and
        // Havok sentinel IDs in the serialized body; zeroing them selects an
        // explicit default body instead and leaves shards without native
        // motion assignment.
        Write(body + 0x10, source.contents);
        Write(body + 0x14, std::uint16_t{0xFFFF});
        Write(body + 0x16, std::uint8_t{0xFF});
        Write(body + 0x88, std::uint16_t{0xFFFF});
    }
    setArray(system + 56, body, 1);

    const std::size_t estimatedCapacity =
        1024u * 1024u + mesh.triangles.size() * 256u + mesh.vertices.size() * 64u;
    PhysicsAsset result;
    result.name = std::move(name);
    result.contents = source.contents;
    result.useCategory = useCategory;
    result.simulationCategory = simulationCategory;

    const auto roundTripData =
        havok.Save(root, (std::min)(kMaximumAllocation, estimatedCapacity), 0x46212B0);
    auto *loaded = static_cast<std::uint8_t *>(havok.Load(roundTripData, 0x46212B0));
    const auto loadedSystem = Read<std::uintptr_t>(loaded + 8);
    const auto loadedBodies =
        loadedSystem ? Read<std::uintptr_t>(reinterpret_cast<void *>(loadedSystem + 56)) : 0;
    if (!loadedSystem || Read<std::uint32_t>(reinterpret_cast<void *>(loadedSystem + 64)) != 1 ||
        !loadedBodies || !Read<std::uintptr_t>(reinterpret_cast<void *>(loadedBodies)))
        throw std::runtime_error("Native model physics round trip lost its body or shape");
    const auto loadedName = Read<std::uintptr_t>(reinterpret_cast<void *>(loadedBodies + 0x18)) &
                            ~std::uintptr_t{1};
    if (!loadedName || std::memcmp(reinterpret_cast<void *>(loadedName), bodyNameData,
                                   bodyName.size() + 1) != 0)
        throw std::runtime_error("Native physics round trip lost its trace body name");
    if (glass)
    {
        const auto *loadedBody = reinterpret_cast<const std::uint8_t *>(loadedBodies);
        if (Read<std::uint32_t>(loadedBody + 0x10) != source.contents ||
            Read<std::uint16_t>(loadedBody + 0x14) != 0xFFFF ||
            Read<std::uint8_t>(loadedBody + 0x16) != 0xFF ||
            Read<std::uint8_t>(loadedBody + 0x17) != 0 ||
            Read<std::uint16_t>(loadedBody + 0x88) != 0xFFFF)
            throw std::runtime_error("Native glass physics round trip lost shipped body policy");
    }

    const auto requireLoadedOne = [&](const std::size_t offset, const std::uint32_t expected,
                                      const char *field) {
        const auto data = Read<std::uintptr_t>(loaded + offset);
        const auto size = Read<std::uint32_t>(loaded + offset + 8);
        if (!data || size != 1 || Read<std::uint32_t>(reinterpret_cast<void *>(data)) != expected)
            throw std::runtime_error(std::string("Native model physics round trip lost ") + field);
    };
    requireLoadedOne(0x10, bodyQualityNameCrc, "body quality lookup");
    requireLoadedOne(0x20, materialNameCrc, "material lookup");
    requireLoadedOne(0x30, motionPropertiesNameCrc, "motion-properties lookup");
    requireLoadedOne(0x60, kServerUsageDefault, "body server usage");
    requireLoadedOne(0x80, kBodyDriverNone, "body driver");
    requireLoadedOne(0x90, simulationCategory, "simulation category");
    for (const auto offset : {std::size_t{0x40}, std::size_t{0x50}})
    {
        if (!Read<std::uintptr_t>(loaded + offset) || Read<std::uint32_t>(loaded + offset + 8) != 1)
            throw std::runtime_error("Native model physics round trip lost event names");
    }

    result.havokData = CompactPhysicsAssetTagfile(roundTripData);

    // Validate the final compact wire form directly.  The offline reflection harness writes the
    // target's TCRF compendium reference but does not install the game's global compendium loader,
    // so attempting to load this compact blob in the harness would be a false negative.  Replay's
    // shipped one-body assets use global item type 5 (int), type 15 (hkStringPtr), patch type 9
    // (hkArray<int>) and patch type 10 (hkArray<hkStringPtr>) at these exact root offsets.
    const TagSection compactRootSection{0, result.havokData.size()};
    const auto compactData = FindTagSection(result.havokData, compactRootSection, "DATA");
    const auto compactIndex = FindTagSection(result.havokData, compactRootSection, "INDX");
    const auto compactItems = FindTagSection(result.havokData, compactIndex, "ITEM");
    const auto compactPatches = FindTagSection(result.havokData, compactIndex, "PTCH");
    if ((compactItems.size - 8) % 12 != 0 || compactData.size < 8 + 224)
        throw std::runtime_error("Native model physics compact tables are invalid");

    struct CompactItem
    {
        std::uint32_t type;
        std::uint32_t offset;
        std::uint32_t count;
    };
    std::vector<CompactItem> items;
    for (std::size_t offset = compactItems.offset + 8;
         offset < compactItems.offset + compactItems.size; offset += 12)
    {
        items.push_back({Read<std::uint32_t>(result.havokData.data() + offset) & 0x00FFFFFFu,
                         Read<std::uint32_t>(result.havokData.data() + offset + 4),
                         Read<std::uint32_t>(result.havokData.data() + offset + 8)});
    }
    if (items.size() < 2 || items[1].type != 7 || items[1].offset != 0 || items[1].count != 1)
        throw std::runtime_error("Native model physics compact root item is invalid");

    const auto *wireRoot = result.havokData.data() + compactData.offset + 8;
    const auto requireWireInt = [&](const std::size_t fieldOffset, const std::uint32_t expected,
                                    const char *field) {
        const auto reference = Read<std::uint64_t>(wireRoot + fieldOffset);
        if (!reference || reference >= items.size())
            throw std::runtime_error(std::string("Native model physics compact root lost ") +
                                     field);
        const auto &item = items[static_cast<std::size_t>(reference)];
        if (item.type != 5 || item.count != 1 ||
            item.offset + sizeof(std::uint32_t) > compactData.size - 8 ||
            Read<std::uint32_t>(wireRoot + item.offset) != expected)
            throw std::runtime_error(std::string("Native model physics compact item invalid: ") +
                                     field);
    };
    const auto requireWireEmptyString = [&](const std::size_t fieldOffset, const char *field) {
        const auto stringReference = Read<std::uint64_t>(wireRoot + fieldOffset);
        if (!stringReference || stringReference >= items.size())
            throw std::runtime_error(std::string("Native model physics compact root lost ") +
                                     field);
        const auto &stringItem = items[static_cast<std::size_t>(stringReference)];
        if (stringItem.type != 15 || stringItem.count != 1 ||
            stringItem.offset + sizeof(std::uint64_t) > compactData.size - 8)
            throw std::runtime_error(std::string("Native model physics compact item invalid: ") +
                                     field);
        const auto charReference = Read<std::uint64_t>(wireRoot + stringItem.offset);
        if (!charReference || charReference >= items.size())
            throw std::runtime_error(std::string("Native model physics compact string lost: ") +
                                     field);
        const auto &charItem = items[static_cast<std::size_t>(charReference)];
        if (charItem.type != 163 || charItem.count != 1 ||
            charItem.offset >= compactData.size - 8 || wireRoot[charItem.offset] != 0)
            throw std::runtime_error(std::string("Native model physics compact string invalid: ") +
                                     field);
    };
    requireWireInt(0x10, bodyQualityNameCrc, "body quality lookup");
    requireWireInt(0x20, materialNameCrc, "material lookup");
    requireWireInt(0x30, motionPropertiesNameCrc, "motion-properties lookup");
    requireWireInt(0x60, kServerUsageDefault, "body server usage");
    requireWireInt(0x80, kBodyDriverNone, "body driver");
    requireWireInt(0x90, simulationCategory, "simulation category");
    requireWireEmptyString(0x40, "body SFX event name");
    requireWireEmptyString(0x50, "body VFX event name");
    if (Read<std::uint64_t>(wireRoot + 0x70) != 0)
        throw std::runtime_error("Native model physics compact constraint policy is not empty");

    std::vector<std::pair<std::uint32_t, std::uint32_t>> patchOffsets;
    for (std::size_t offset = compactPatches.offset + 8;
         offset < compactPatches.offset + compactPatches.size;)
    {
        if (offset + 8 > compactPatches.offset + compactPatches.size)
            throw std::runtime_error("Native model physics compact patch table is truncated");
        const auto type = Read<std::uint32_t>(result.havokData.data() + offset);
        const auto count = Read<std::uint32_t>(result.havokData.data() + offset + 4);
        offset += 8;
        if (count > (compactPatches.offset + compactPatches.size - offset) / 4)
            throw std::runtime_error("Native model physics compact patch count is invalid");
        for (std::uint32_t index = 0; index < count; ++index, offset += 4)
            patchOffsets.emplace_back(type, Read<std::uint32_t>(result.havokData.data() + offset));
    }
    const auto requirePatch = [&](const std::uint32_t type, const std::uint32_t offset) {
        if (std::ranges::find(patchOffsets, std::pair{type, offset}) == patchOffsets.end())
            throw std::runtime_error("Native model physics compact root patch is missing");
    };
    for (const auto offset : {0x10u, 0x20u, 0x30u, 0x60u, 0x80u, 0x90u})
        requirePatch(9, offset);
    for (const auto offset : {0x40u, 0x50u})
        requirePatch(10, offset);

    const auto bodyItem = std::ranges::find_if(items, [](const CompactItem &item) {
        return item.type == 39 && item.count == 1;
    });
    if (bodyItem == items.end() || bodyItem->offset + 192 > compactData.size - 8)
        throw std::runtime_error("Native physics compact body is missing");
    const auto nameReference = Read<std::uint64_t>(wireRoot + bodyItem->offset + 0x18);
    if (!nameReference || nameReference >= items.size())
        throw std::runtime_error("Native physics compact trace body name is missing");
    const auto &nameItem = items[static_cast<std::size_t>(nameReference)];
    if (nameItem.type != 163 || nameItem.count != bodyName.size() + 1 ||
        nameItem.offset + nameItem.count > compactData.size - 8 ||
        std::memcmp(wireRoot + nameItem.offset, bodyNameData, bodyName.size() + 1) != 0)
        throw std::runtime_error("Native physics compact trace body name is invalid");
    requirePatch(15, bodyItem->offset + 0x18);

    const std::array<std::uint8_t, 4> typeChunk{'T', 'Y', 'P', 'E'};
    const std::array<std::uint8_t, 4> compendiumChunk{'T', 'C', 'R', 'F'};
    const auto type = std::search(result.havokData.begin(), result.havokData.end(),
                                  typeChunk.begin(), typeChunk.end());
    const auto compendium = std::search(result.havokData.begin(), result.havokData.end(),
                                        compendiumChunk.begin(), compendiumChunk.end());
    const auto compendiumSignature =
        compendium != result.havokData.end() &&
                std::distance(compendium, result.havokData.end()) >= 12
            ? Read<std::uint64_t>(&*compendium + compendiumChunk.size())
            : 0;
    zt::info("collision: native PhysicsAsset type compendium 0x%016llX",
             static_cast<unsigned long long>(compendiumSignature));
    if (type != result.havokData.end() || compendium == result.havokData.end() ||
        compendiumSignature != kReplayTypeCompendiumSignature)
        throw std::runtime_error(
            "Native model physics did not use Replay's compact type-compendium format");

    zt::info("collision: baked native PhysicsAsset '%s' (%zu vertices, %zu triangles, %zu bytes)",
             result.name.c_str(), mesh.vertices.size(), mesh.triangles.size(),
             result.havokData.size());
    return result;
}

} // namespace iw8::havok
