#include "write_weapon.h"
#include "common/ff_io.h"
#include "common/json.hpp"
#include "common/log.h"
#include "convert/conv_xsurface.h"
#include "convert/xsurface_convert.h"
#include "dumpsrc/xmodel_dump.h"
#include "dumpsrc/xse_dump.h"
#include "iw8_zone.h"
#include "replay_render.h"
#include "write_xsurface.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>

namespace iw8
{
namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;
using Vec = std::array<float, 3>;
using AssetKey = std::pair<unsigned, std::string>;
using Replacements = std::map<AssetKey, std::string>;

Json readJson(const fs::path &path)
{
    if (fs::file_size(path) > 64 * 1024 * 1024)
        throw std::runtime_error("weapon input exceeds 64 MiB: " + path.string());
    std::ifstream file(path);
    return Json::parse(file);
}

std::vector<uint8_t> unhex(const std::string &text)
{
    if (text.size() % 2 || text.size() > 16 * 1024 * 1024)
        throw std::runtime_error("invalid weapon record length");
    const auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("invalid weapon record hex");
    };
    std::vector<uint8_t> result(text.size() / 2);
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = uint8_t(digit(text[i * 2]) * 16 + digit(text[i * 2 + 1]));
    return result;
}

template <typename T> void put(std::vector<uint8_t> &data, size_t offset, T value)
{
    if (offset > data.size() || sizeof(value) > data.size() - offset)
        throw std::runtime_error("weapon relocation outside its record");
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::string text(const Json &value)
{
    auto result = value.get<std::string>();
    if (result.size() > 4095 || result.find('\0') != std::string::npos)
        throw std::runtime_error("invalid weapon string");
    return result;
}

// Comma assets are the engine's native name-only dependencies. Their layouts are
// checked against the Replay asset size table, not another IW8 build's enum.
size_t dependencySize(unsigned type)
{
    switch (type)
    {
    case 7:
        return 0xA0;
    case 9:
        return 0x2B0;
    case 11:
        return 0x78;
    case 19:
        return 0xE8;
    case 22:
        return 0x200;
    case 42:
        return 0x3C8;
    case 40:
        return 0x290;
    case 44:
        return 0x80;
    case 58:
        return 0x90;
    case 68:
        return 0xA8;
    case 75:
        return 0x38;
    case 77:
        return 0x40;
    case 78:
        return 0xB0;
    case 76:
        return 0x90;
    case 79:
        return 0x10;
    case 86:
        return 0x48;
    case 97:
        return 0x40;
    case 102:
        return 0x40;
    default:
        throw std::runtime_error("unsupported melee dependency type " + std::to_string(type));
    }
}

void dependencyBody(ZoneWriter &w, unsigned type, const std::string &name)
{
    const auto size = dependencySize(type);
    std::vector<uint8_t> header(size);
    put(header, 0, PTR_FOLLOWS);
    w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    w.align(7);
    w.write(header.data(), header.size());
    w.pushStream(XFILE_BLOCK_VIRTUAL);
    w.writeStr("," + name);
    w.popStream();
    w.popStream();
}

void dependency(ZoneWriter &writer, unsigned type, const std::string &name)
{
    writer.add(static_cast<IW8_XAssetType>(type), name,
               [=](ZoneWriter &w) { dependencyBody(w, type, name); });
}

void prepareRecord(ZoneWriter &writer, Json &record,
                   std::set<std::pair<unsigned, std::string>> &dependencies,
                   const Replacements &models, unsigned depth = 0)
{
    if (depth > 16 || !record.at("fixups").is_array())
        throw std::runtime_error("invalid melee reference tree");
    auto bytes = unhex(record.at("data"));
    const size_t alignment = record.at("alignment");
    if (!alignment || alignment > 16 || (alignment & (alignment - 1)))
        throw std::runtime_error("invalid melee record alignment");
    std::set<size_t> touched;
    for (auto &f : record["fixups"])
    {
        const size_t offset = f.at("offset");
        const auto kind = f.at("kind").get<std::string>();
        const size_t width = kind == "script" ? 4 : 8;
        for (size_t b = 0; b < width; ++b)
            if (offset + b >= bytes.size() || !touched.insert(offset + b).second ||
                bytes[offset + b])
                throw std::runtime_error("overlapping, nonzero or out-of-range melee relocation");
        if (kind == "asset")
        {
            const unsigned type = f.at("asset_type");
            if (dependencySize(type) != f.at("size").get<size_t>())
                throw std::runtime_error("reference asset size does not match Replay");
            auto name = text(f.at("name"));
            if (name.empty() || name.front() == ',')
                throw std::runtime_error("invalid dependency name");
            if (models.contains({type, name}))
                f["name"] = models.at({type, name});
            else
                dependencies.emplace(type, name);
        }
        else if (kind == "script")
            f["index"] = writer.internScriptString(text(f.at("text")));
        else if (kind == "string")
            (void)text(f.at("text"));
        else if (kind == "record")
            prepareRecord(writer, f, dependencies, models, depth + 1);
        else
            throw std::runtime_error("unknown melee relocation kind");
    }
    // D9A580 visits each accuracy graph's name and knots together, even though
    // the two pointer arrays are separate in the structure.
    const auto recordType = record.value("record_type", std::string());
    const auto order = [definition = recordType == "WeaponDef" ||
                                        (recordType.empty() && bytes.size() == 0x14B0),
                        attachment = recordType == "WeaponAttachment" ||
                                        (recordType.empty() && bytes.size() == 0x3C8)](size_t offset) {
        if (definition && offset == 0xC18)
            return size_t(0xC09);
        // E0E480 visits general (+118) before laser (+108) and post (+110).
        if (attachment && offset == 0x118)
            return size_t(0x107);
        return offset;
    };
    std::sort(record["fixups"].begin(), record["fixups"].end(), [&](const auto &a, const auto &b) {
        return order(a.at("offset").template get<size_t>()) <
               order(b.at("offset").template get<size_t>());
    });
}

void emitRecord(ZoneWriter &w, const Json &record, bool root = false,
                const AssetKey *owner = nullptr)
{
    auto bytes = unhex(record.at("data"));
    for (const auto &f : record.at("fixups"))
    {
        const size_t offset = f.at("offset");
        const auto kind = f.at("kind").get<std::string>();
        if (kind == "asset")
        {
            const auto type = static_cast<IW8_XAssetType>(f.at("asset_type").get<int>());
            const auto name = f.at("name").get<std::string>();
            if (owner && owner->first == static_cast<unsigned>(type) && owner->second == name)
                put(bytes, offset, PTR_INSERT);
            else
                put(bytes, offset, w.assetAlias(type, name));
        }
        else if (kind == "script")
            put(bytes, offset, f.at("index").get<uint32_t>());
        else
            put(bytes, offset, PTR_FOLLOWS);
    }
    if (root)
        w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    w.align(record.at("alignment").get<size_t>() - 1);
    w.write(bytes.data(), bytes.size());
    if (root)
        w.pushStream(XFILE_BLOCK_VIRTUAL);
    for (const auto &f : record.at("fixups"))
    {
        if (f.at("kind") == "string")
            w.writeStr(text(f.at("text")));
        else if (f.at("kind") == "record")
            emitRecord(w, f, false, owner);
        else if (f.at("kind") == "asset" && owner &&
                 owner->first == f.at("asset_type").get<unsigned>() &&
                 owner->second == text(f.at("name")))
        {
            // Stock global_stream_mp represents this edge with a distinct comma
            // reference. The insert slot lets Replay publish that native handle
            // while loading the inline name-only asset body below.
            w.align(7);
            w.reserveCalc(8);
            dependencyBody(w, owner->first, owner->second);
        }
    }
    if (root)
    {
        w.popStream();
        w.popStream();
    }
}

Vec normal(Vec v)
{
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!std::isfinite(length) || length < 1e-7f)
        throw std::runtime_error("degenerate OBJ triangle");
    for (auto &x : v)
        x /= length;
    return v;
}
Vec cross(const Vec &a, const Vec &b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
uint32_t packedVector(const Vec &v)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 3; ++i)
        value |= uint32_t(std::clamp(std::lround(v[i] * 127.0f + 127.0f), 0l, 254l)) << (8 * i);
    return value;
}

struct AnimationData
{
    struct Notify
    {
        uint32_t name;
        float time;
    };
    std::string name;
    std::vector<uint32_t> names;
    std::vector<uint8_t> dataByte;
    std::vector<int16_t> dataShort;
    std::vector<int32_t> dataInt;
    std::vector<int16_t> randomDataShort;
    std::vector<uint16_t> indices;
    std::vector<Notify> notifies;
    uint16_t numFrames{};
    float frameRate{};
    uint8_t flags{};
    uint8_t assetType{};
    uint8_t ikType{};
    uint8_t fingerPoseType{};
};
static_assert(sizeof(AnimationData::Notify) == 8);

void appendFloat(std::vector<int32_t> &values, float value)
{
    int32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    values.push_back(bits);
}

void appendAnimationIndices(AnimationData &data, size_t count)
{
    if (!count || count > 65536)
        throw std::runtime_error("animation track has an invalid frame count");
    data.dataShort.push_back(static_cast<int16_t>(count - 1));
    if (data.numFrames < 256)
    {
        for (size_t i = 0; i < count; ++i)
            data.dataByte.push_back(static_cast<uint8_t>(i));
    }
    else if (count >= 65)
    {
        for (size_t i = 0; i < count; ++i)
            data.indices.push_back(static_cast<uint16_t>(i));
        const size_t checkpoints = ((count - 2) / 256) + 1;
        for (size_t i = 0; i < checkpoints; ++i)
            data.dataShort.push_back(static_cast<int16_t>(i * 256));
        data.dataShort.push_back(static_cast<int16_t>(count - 1));
    }
    else
    {
        for (size_t i = 0; i < count; ++i)
            data.dataShort.push_back(static_cast<int16_t>(i));
    }
}

AnimationData animationData(ZoneWriter &writer, const std::string &name, const Json &clip)
{
    if (clip.at("format") != "replay-animation-source-v1" || !clip.at("tracks").is_array() ||
        clip.at("tracks").empty() || clip.at("tracks").size() > 255)
        throw std::runtime_error("invalid imported animation");
    AnimationData data;
    data.name = name;
    const double rate = clip.at("fps").get<double>();
    const double duration = clip.at("duration").get<double>();
    if (!std::isfinite(rate) || rate <= 0 || rate > 240 || !std::isfinite(duration) ||
        duration <= 0 || duration > 120)
        throw std::runtime_error("animation rate or duration is outside the supported range");
    data.frameRate = static_cast<float>(rate);
    data.flags = clip.value("loop", false) ? 1 : 0;
    const auto byteSetting = [&clip](const char *key, int fallback) {
        const int value = clip.value(key, fallback);
        if (value < 0 || value > UINT8_MAX)
            throw std::runtime_error(std::string("animation ") + key + " is outside the byte range");
        return static_cast<uint8_t>(value);
    };
    data.assetType = byteSetting("asset_type", 6);
    data.ikType = byteSetting("ik_type", 1);
    data.fingerPoseType = byteSetting("finger_pose_type", 1);

    size_t frameCount = 0;
    std::set<std::string> boneNames;
    for (const auto &track : clip.at("tracks"))
    {
        const auto bone = text(track.at("bone"));
        if (bone.empty() || bone.size() > 63 || !boneNames.emplace(bone).second ||
            !track.at("translations").is_array() || !track.at("quaternions").is_array() ||
            track.at("translations").size() != track.at("quaternions").size())
            throw std::runtime_error("invalid animation bone track");
        if (!frameCount)
            frameCount = track.at("translations").size();
        if (track.at("translations").size() != frameCount)
            throw std::runtime_error("animation tracks have different frame counts");
        data.names.push_back(writer.internScriptString(bone));
    }
    if (frameCount < 2 || frameCount > 65536)
        throw std::runtime_error("animation must contain 2..65536 sampled frames");
    data.numFrames = static_cast<uint16_t>(frameCount - 1);

    // Every imported track is emitted as a precise, fully keyed transform.
    // This avoids making assumptions about which authored bind-pose components
    // are safe to omit. Replay consumes these as FULL_QUAT/FULL_TRANS tracks.
    for (const auto &track : clip.at("tracks"))
    {
        appendAnimationIndices(data, frameCount);
        std::array<double, 4> previous{0, 0, 0, 1};
        bool first = true;
        for (const auto &frame : track.at("quaternions"))
        {
            if (!frame.is_array() || frame.size() != 4)
                throw std::runtime_error("animation quaternion must have four components");
            std::array<double, 4> q{};
            double length = 0;
            for (size_t i = 0; i < 4; ++i)
            {
                q[i] = frame[i].get<double>();
                if (!std::isfinite(q[i]))
                    throw std::runtime_error("animation quaternion is not finite");
                length += q[i] * q[i];
            }
            length = std::sqrt(length);
            if (length < 1e-9)
                throw std::runtime_error("animation quaternion has zero length");
            for (auto &component : q)
                component /= length;
            if (!first)
            {
                double dot = 0;
                for (size_t i = 0; i < 4; ++i)
                    dot += previous[i] * q[i];
                if (dot < 0)
                    for (auto &component : q)
                        component = -component;
            }
            previous = q;
            first = false;
            for (const auto component : q)
                data.randomDataShort.push_back(static_cast<int16_t>(
                    std::clamp(std::lround(component * 32767.0), -32767l, 32767l)));
        }
    }

    for (size_t bone = 0; bone < clip.at("tracks").size(); ++bone)
    {
        const auto &frames = clip.at("tracks")[bone].at("translations");
        data.dataByte.push_back(static_cast<uint8_t>(bone));
        appendAnimationIndices(data, frameCount);
        std::array<double, 3> low{DBL_MAX, DBL_MAX, DBL_MAX};
        std::array<double, 3> high{-DBL_MAX, -DBL_MAX, -DBL_MAX};
        for (const auto &frame : frames)
        {
            if (!frame.is_array() || frame.size() != 3)
                throw std::runtime_error("animation translation must have three components");
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const auto value = frame[axis].get<double>();
                if (!std::isfinite(value) || std::abs(value) > 1000000)
                    throw std::runtime_error("animation translation is outside the supported range");
                low[axis] = (std::min)(low[axis], value);
                high[axis] = (std::max)(high[axis], value);
            }
        }
        std::array<double, 3> scale{};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            scale[axis] = (high[axis] - low[axis]) / 65535.0;
            appendFloat(data.dataInt, static_cast<float>(low[axis]));
        }
        for (const auto value : scale)
            appendFloat(data.dataInt, static_cast<float>(value));
        for (const auto &frame : frames)
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const auto encoded = scale[axis] == 0 ? 0l :
                    std::clamp(std::lround((frame[axis].get<double>() - low[axis]) / scale[axis]),
                               0l, 65535l);
                data.randomDataShort.push_back(static_cast<int16_t>(static_cast<uint16_t>(encoded)));
            }
    }

    const auto notetracks = clip.value("notetracks", Json::array());
    if (!notetracks.is_array() || notetracks.size() > 255)
        throw std::runtime_error("animation has too many notetracks");
    for (const auto &note : notetracks)
    {
        const auto noteName = text(note.at("name"));
        const double seconds = note.at("time").get<double>();
        if (noteName.empty() || noteName.size() > 63 || !std::isfinite(seconds) ||
            seconds < 0 || seconds > duration)
            throw std::runtime_error("invalid animation notetrack");
        data.notifies.push_back({writer.internScriptString(noteName),
                                 static_cast<float>(seconds / duration)});
    }
    if (data.dataShort.size() > UINT16_MAX || data.dataInt.size() > UINT16_MAX)
        throw std::runtime_error("animation flat data exceeds Replay count fields");
    return data;
}

void addAnimation(ZoneWriter &writer, const std::string &name, const Json &clip)
{
    auto data = animationData(writer, name, clip);
    writer.add(static_cast<IW8_XAssetType>(7), name, [data = std::move(data)](ZoneWriter &w) {
        std::vector<uint8_t> header(160);
        put(header, 0, PTR_FOLLOWS);
        put(header, 8, data.names.empty() ? PTR_NULL : PTR_FOLLOWS);
        put(header, 16, data.dataByte.empty() ? PTR_NULL : PTR_FOLLOWS);
        put(header, 24, data.dataShort.empty() ? PTR_NULL : PTR_FOLLOWS);
        put(header, 32, data.dataInt.empty() ? PTR_NULL : PTR_FOLLOWS);
        put(header, 40, data.randomDataShort.empty() ? PTR_NULL : PTR_FOLLOWS);
        put(header, 48, PTR_NULL);
        put(header, 56, PTR_NULL);
        put(header, 64, data.indices.empty() ? PTR_NULL : PTR_FOLLOWS);
        put(header, 72, data.notifies.empty() ? PTR_NULL : PTR_FOLLOWS);
        put(header, 80, PTR_NULL);
        put(header, 88, static_cast<uint32_t>(data.randomDataShort.size()));
        put(header, 92, uint32_t(0));
        put(header, 96, static_cast<uint32_t>(data.indices.size()));
        put(header, 100, data.frameRate);
        put(header, 104, data.numFrames ? data.frameRate / data.numFrames : 0.0f);
        put(header, 108, static_cast<uint32_t>(data.dataByte.size()));
        put(header, 112, static_cast<uint16_t>(data.dataShort.size()));
        put(header, 114, static_cast<uint16_t>(data.dataInt.size()));
        put(header, 116, uint16_t(0));
        put(header, 118, data.numFrames);
        put(header, 120, data.flags);
        header[121 + 2] = static_cast<uint8_t>(data.names.size()); // FULL_QUAT
        header[121 + 6] = static_cast<uint8_t>(data.names.size()); // FULL_TRANS
        header[121 + 9] = static_cast<uint8_t>(data.names.size()); // ALL
        header[131] = static_cast<uint8_t>(data.notifies.size());
        header[132] = data.assetType;
        header[133] = data.ikType;
        header[134] = data.fingerPoseType;
        w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
        w.align(7);
        w.write(header.data(), header.size());
        w.pushStream(XFILE_BLOCK_VIRTUAL);
        w.writeStr(data.name);
        if (!data.names.empty())
        {
            w.align(3);
            w.write(data.names.data(), data.names.size() * sizeof(uint32_t));
        }
        if (!data.notifies.empty())
        {
            w.align(3);
            w.write(data.notifies.data(), data.notifies.size() * sizeof(AnimationData::Notify));
        }
        if (!data.dataByte.empty())
            w.write(data.dataByte.data(), data.dataByte.size());
        if (!data.dataShort.empty())
        {
            w.align(1);
            w.write(data.dataShort.data(), data.dataShort.size() * sizeof(int16_t));
        }
        if (!data.dataInt.empty())
        {
            w.align(3);
            w.write(data.dataInt.data(), data.dataInt.size() * sizeof(int32_t));
        }
        if (!data.randomDataShort.empty())
        {
            w.align(1);
            w.write(data.randomDataShort.data(), data.randomDataShort.size() * sizeof(int16_t));
        }
        if (!data.indices.empty())
        {
            w.align(1);
            w.write(data.indices.data(), data.indices.size() * sizeof(uint16_t));
        }
        w.popStream();
        w.popStream();
    });
}

std::vector<uint8_t> readBinary(const fs::path &path, size_t limit = 128 * 1024 * 1024)
{
    const auto size = fs::file_size(path);
    if (!size || size > limit)
        throw std::runtime_error("resident sound bank is empty or exceeds 128 MiB");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw std::runtime_error("could not read resident sound bank: " + path.string());
    return bytes;
}

struct AliasIndex
{
    uint16_t value = UINT16_MAX;
    uint16_t next = UINT16_MAX;
};

std::vector<AliasIndex> makeAliasIndex(const Json &sounds)
{
    const size_t count = sounds.size();
    std::vector<AliasIndex> result(count);
    const auto bucketFor = [&](uint16_t value) {
        return sounds.at(value).at("alias_id").get<uint32_t>() % count;
    };
    const auto empty = [&]() -> uint16_t {
        for (uint16_t i = 0; i < result.size(); ++i)
            if (result[i].value == UINT16_MAX)
                return i;
        throw std::runtime_error("sound alias index table overflow");
    };
    for (uint16_t value = 0; value < count; ++value)
    {
        const uint16_t bucket = static_cast<uint16_t>(bucketFor(value));
        if (result[bucket].value == UINT16_MAX)
        {
            result[bucket].value = value;
            continue;
        }
        if (bucketFor(result[bucket].value) == bucket)
        {
            uint16_t tail = bucket;
            while (result[tail].next != UINT16_MAX)
                tail = result[tail].next;
            const auto free = empty();
            result[free].value = value;
            result[tail].next = free;
            continue;
        }
        // A prior collision borrowed this alias's home bucket. Relocate that
        // chain node, repair its predecessor, then use the canonical bucket.
        const uint16_t displaced = result[bucket].value;
        const uint16_t owner = static_cast<uint16_t>(bucketFor(displaced));
        uint16_t predecessor = owner;
        while (result[predecessor].next != bucket)
        {
            if (result[predecessor].next == UINT16_MAX)
                throw std::runtime_error("invalid sound alias collision chain");
            predecessor = result[predecessor].next;
        }
        const auto free = empty();
        result[free] = result[bucket];
        result[predecessor].next = free;
        result[bucket] = {value, UINT16_MAX};
    }
    return result;
}

void addResidentSoundAssets(ZoneWriter &writer, const std::string &bankName,
                            std::vector<uint8_t> bankBytes, const Json &sounds,
                            const std::string &zone)
{
    if (sounds.empty() || sounds.size() > UINT16_MAX)
        throw std::runtime_error("invalid native sound alias collection");
    const auto streamName = zone + "_all_loaded";
    writer.add(static_cast<IW8_XAssetType>(102), streamName,
               [streamName, bytes = std::move(bankBytes)](ZoneWriter &w) {
                   std::vector<uint8_t> root(64);
                   put(root, 0, PTR_FOLLOWS);
                   put(root, 40, PTR_FOLLOWS);
                   put(root, 56, static_cast<uint32_t>(bytes.size()));
                   root[61] = 2; // resident data, rather than an XPak entry
                   w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
                   w.align(7);
                   w.write(root.data(), root.size());
                   w.pushStream(XFILE_BLOCK_VIRTUAL);
                   w.writeStr(streamName);
                   w.align(15);
                   w.write(bytes.data(), bytes.size());
                   w.popStream();
                   w.popStream();
               });
    const auto index = makeAliasIndex(sounds);
    writer.add(static_cast<IW8_XAssetType>(22), bankName,
               [bankName, streamName, zone, sounds, index](ZoneWriter &w) {
                   std::vector<uint8_t> bank(512);
                   for (size_t offset : {size_t(0), size_t(8), size_t(16), size_t(24), size_t(40), size_t(48)})
                       put(bank, offset, PTR_FOLLOWS);
                   put(bank, 32, static_cast<uint32_t>(sounds.size()));
                   put(bank, 496, w.assetAlias(static_cast<IW8_XAssetType>(102), streamName));
                   w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
                   w.align(7);
                   w.write(bank.data(), bank.size());
                   w.pushStream(XFILE_BLOCK_VIRTUAL);
                   w.writeStr(bankName);
                   w.writeStr(zone);
                   w.writeStr("eng");
                   w.writeStr("all");
                   w.align(7);
                   for (const auto &sound : sounds)
                   {
                       std::vector<uint8_t> list(32);
                       put(list, 0, PTR_FOLLOWS);
                       put(list, 8, sound.at("alias_id").get<uint32_t>());
                       put(list, 16, PTR_FOLLOWS);
                       put(list, 24, int32_t(1));
                       w.write(list.data(), list.size());
                   }
                   for (const auto &sound : sounds)
                   {
                       const auto alias = text(sound.at("alias"));
                       const auto assetFile = text(sound.at("asset_file"));
                       const auto preset = text(sound.at("preset"));
                       const float volume = sound.at("volume").get<float>();
                       const float pitch = sound.at("pitch").get<float>();
                       const float maximum = sound.at("distance").get<float>();
                       const double duration = sound.at("duration").get<double>();
                       if (!std::isfinite(volume) || !std::isfinite(pitch) || !std::isfinite(maximum) ||
                           !std::isfinite(duration) || volume < 0 || volume > 4 || pitch < .25f ||
                           pitch > 4 || maximum < 0 || maximum > 100000 || duration <= 0)
                           throw std::runtime_error("invalid native sound mix settings");
                       uint32_t flags = 15360, volumeGroup = 63, speakerMap = 1065857480;
                       float minimum = std::min(5000.0f, maximum), smartPan = 1.0f, reverb = .25f;
                       if (preset == "mechanical")
                       {
                           flags = 31744;
                           volumeGroup = 18;
                           minimum = std::min(24.0f, maximum);
                           speakerMap = 4135636924;
                           reverb = .5f;
                       }
                       else if (preset == "ui")
                       {
                           flags = 29696;
                           volumeGroup = 18;
                           minimum = 0;
                           speakerMap = 4135636924;
                           reverb = 0;
                       }
                       else if (preset != "weapon_player" && preset != "weapon_world")
                           throw std::runtime_error("invalid native sound preset");
                       std::vector<uint8_t> head(232);
                       put(head, 0, PTR_FOLLOWS);
                       put(head, 24, PTR_FOLLOWS);
                       put(head, 32, sound.at("alias_id").get<uint32_t>());
                       put(head, 40, sound.at("asset_id").get<uint32_t>());
                       put(head, 72, volume);
                       put(head, 76, volume);
                       put(head, 80, volumeGroup);
                       put(head, 84, pitch);
                       put(head, 88, pitch);
                       put(head, 92, minimum);
                       put(head, 96, minimum);
                       put(head, 100, maximum);
                       put(head, 108, flags);
                       put(head, 112, 1.0f);
                       for (size_t offset : {size_t(132), size_t(136), size_t(140), size_t(144)})
                           put(head, offset, uint32_t(4135636924));
                       put(head, 160, speakerMap);
                       put(head, 164, reverb);
                       put(head, 168, 1.0f);
                       put(head, 172, uint32_t(4135636924));
                       put(head, 176, uint32_t(4135636924));
                       put(head, 188, smartPan);
                       put(head, 212, 1.0f);
                       put(head, 216, 0.25118864f);
                       put(head, 224, static_cast<int16_t>(std::clamp(std::ceil(duration * 10.0), 1.0, 32767.0)));
                       w.writeStr(alias);
                       w.align(7);
                       w.write(head.data(), head.size());
                       w.writeStr(alias);
                       w.writeStr(assetFile);
                   }
                   w.align(1);
                   w.write(index.data(), index.size() * sizeof(AliasIndex));
                   w.popStream();
                   w.popStream();
               });
}

// Bone names and bind transforms are shared by the authoring view and native XModel.
convert::xmodel::Iw8XModelRecord modelRecord(const Json &j, const std::string &name)
{
    convert::xmodel::Iw8XModelRecord model;
    model.name = name;
    if (j.at("bones").size() > 128)
        throw std::runtime_error("melee model has too many bones");
    model.numBones = static_cast<uint8_t>(j.at("bones").size());
    model.numRootBones = j.at("root_bones");
    model.boneNames = j.at("bones").get<std::vector<std::string>>();
    model.skeleton.parentList = j.at("parents").get<std::vector<uint8_t>>();
    model.skeleton.quats = j.at("quats").get<std::vector<std::array<int16_t, 4>>>();
    model.skeleton.trans = j.at("translations").get<std::vector<Vec>>();
    model.skeleton.partClassification = j.at("classification").get<std::vector<uint8_t>>();
    for (const auto &pose : j.at("bind_pose"))
        model.skeleton.baseMat.push_back({pose.at("quat").get<std::array<float, 4>>(),
                                          pose.at("translation").get<Vec>(), pose.at("weight")});
    model.collLod = UINT8_MAX;
    model.shadowCutoffLod = 6;
    model.numLods = 1;
    model.numsurfs = 1;
    model.himipRadiusInvSq = {0};
    model.materials = {text(j.at("material"))};
    model.skeleton.boneInfo.resize(model.numBones);
    return model;
}

replayrender::Mesh weaponMaterial(const fs::path &path, const std::string &base)
{
    const auto j = readJson(path);
    if (j.at("format") != "replay-weapon-material-v1")
        throw std::runtime_error("expected replay-weapon-material-v1");
    replayrender::Mesh material;
    material.material = base + "/material";
    // Exact Replay stock shader profile: every depth, shadow and lit technique
    // maps VERTDECL_PACKED (2) to a valid PSO. m2o weapon shaders require 5/6.
    material.techset = "m/lit_3_lit_rpl_ta1_804040_1042000000000030_0_1_1_0_0_13814015b_0_0_1_0_0";
    material.materialInfo = unhex(j.at("info"));
    material.constants = unhex(j.at("constants"));
    material.bufferIndices = unhex(j.at("bufferIndices"));
    material.buffers.resize(1);
    material.buffers[0][3] = unhex(j.at("pixelConstants"));
    if (material.materialInfo.size() != 32 || material.constants.size() != 80 ||
        material.bufferIndices.size() != 195 || material.buffers[0][3].size() != 64)
        throw std::runtime_error("invalid single-UV weapon material profile");
    uint32_t geometryType{};
    std::memcpy(&geometryType, material.materialInfo.data() + 12, sizeof(geometryType));
    if (geometryType != 0x80000 || material.materialInfo[20] != 3 ||
        material.materialInfo[21] != 4 || material.materialInfo[22] != 1 ||
        material.materialInfo[23] != 0)
        throw std::runtime_error("weapon material must use the single-UV model layout");
    const auto &images = j.at("images");
    if (images.size() != 3)
        throw std::runtime_error(
            "weapon material requires color/specular, normal/gloss and emissive images");
    const std::array<unsigned, 3> slots{0, 9, 59};
    for (size_t index = 0; index < images.size(); ++index)
    {
        const auto &source = images[index];
        replayrender::Image image;
        image.name = base + "/image_" + std::to_string(slots[index]);
        const unsigned width = source.at("width"), height = source.at("height");
        if (!width || !height || width > 2048 || height > 2048)
            throw std::runtime_error("weapon material image dimensions must be 1..2048");
        image.width = uint16_t(width);
        image.height = uint16_t(height);
        image.format = index == 1 ? 6 : 7; // RGBA8 linear normal/gloss, sRGB color/emissive.
        const auto file = text(source.at("file"));
        if (fs::path(file).filename() != file || file.find("..") != std::string::npos)
            throw std::runtime_error("weapon material image must be adjacent to its definition");
        const auto imagePath = path.parent_path() / file;
        const size_t bytes = size_t(width) * height * 4;
        if (fs::file_size(imagePath) != bytes)
            throw std::runtime_error(
                "weapon material image byte count does not match RGBA8 dimensions");
        image.pixels.resize(bytes);
        std::ifstream input(imagePath, std::ios::binary);
        if (!input.read(reinterpret_cast<char *>(image.pixels.data()), bytes))
            throw std::runtime_error("could not read weapon material image");
        material.textureHeaders.resize((index + 1) * 8);
        material.textureHeaders[index * 8] = uint8_t(slots[index]);
        material.images.push_back(image.name);
        material.imageDefinitions.push_back(std::move(image));
    }
    return material;
}

void addModel(ZoneWriter &w, const fs::path &obj, const Json &j, const std::string &name,
              bool textured = false)
{
    if (fs::file_size(obj) > 40 * 1024 * 1024)
        throw std::runtime_error("weapon OBJ exceeds 40 MiB");
    auto model = modelRecord(j, name);
    const unsigned bone = j.at("rigid_bone");
    if (model.numBones == 0 || model.numBones > 128 || bone >= model.numBones)
        throw std::runtime_error("invalid melee skeleton/rigid bone");
    const auto matrix = j.at("transform").get<std::array<std::array<float, 4>, 3>>();
    for (const auto &row : matrix)
        for (float value : row)
            if (!std::isfinite(value))
                throw std::runtime_error("nonfinite OBJ transform");
    const auto minor = cross(Vec{matrix[1][0], matrix[1][1], matrix[1][2]},
                             Vec{matrix[2][0], matrix[2][1], matrix[2][2]});
    const float determinant =
        matrix[0][0] * minor[0] + matrix[0][1] * minor[1] + matrix[0][2] * minor[2];
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f)
        throw std::runtime_error("singular OBJ transform");
    const auto boneIndex = [&](const std::string &boneName) -> unsigned {
        const auto found = std::find(model.boneNames.begin(), model.boneNames.end(), boneName);
        if (found == model.boneNames.end())
            throw std::runtime_error("mesh references an unknown bone: " + boneName);
        return static_cast<unsigned>(found - model.boneNames.begin());
    };
    using Weights = std::vector<std::pair<unsigned, float>>;
    std::vector<Weights> vertexWeights;
    // Interchange skeletons do not carry a Replay-native bind-space contract.
    // Imported weapon geometry therefore uses the rigid named-part path unless
    // an author has explicitly supplied native-space blend weights.
    const auto nativeSkinWeights = j.value("native_skin_weights", false);
    for (const auto &weights : nativeSkinWeights ? j.value("vertex_weights", Json::array()) : Json::array())
    {
        if (!weights.is_array() || weights.empty() || weights.size() > 4)
            throw std::runtime_error("a skinned vertex requires one to four bone influences");
        Weights parsed;
        float total = 0;
        std::set<unsigned> used;
        for (const auto &influence : weights)
        {
            const auto index = boneIndex(text(influence.at("bone")));
            const auto weight = influence.at("weight").get<float>();
            if (!std::isfinite(weight) || weight <= 0 || !used.insert(index).second)
                throw std::runtime_error("invalid or duplicate vertex bone influence");
            parsed.emplace_back(index, weight);
            total += weight;
        }
        if (!std::isfinite(total) || total <= 0)
            throw std::runtime_error("invalid vertex weight total");
        for (auto &[index, weight] : parsed)
            weight /= total;
        // Keep the largest influence implicit in the native blend stream.
        std::stable_sort(parsed.begin(), parsed.end(), [](auto a, auto b) { return a.second > b.second; });
        vertexWeights.push_back(std::move(parsed));
    }
    std::map<std::string, unsigned> partBones;
    const auto assignments = j.value("part_bones", Json::object());
    for (const auto &[part, target] : assignments.items())
        partBones.emplace(part, boneIndex(text(target)));
    struct Part
    {
        std::string name;
        dumpsrc::XseSurface surface;
        std::vector<Weights> weights;
    };
    std::vector<Part> parts;
    std::string group = "default";
    std::vector<Vec> positions, normals;
    std::vector<std::array<float, 2>> texcoords;
    Vec minimum{1e20f, 1e20f, 1e20f}, maximum{-1e20f, -1e20f, -1e20f};
    std::ifstream file(obj);
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream row(line);
        std::string command;
        row >> command;
        if (command == "g" || command == "o")
        {
            std::getline(row >> std::ws, group);
            if (group.empty())
                group = "default";
        }
        else if (command == "v")
        {
            Vec p{}, result{};
            if (!(row >> p[0] >> p[1] >> p[2]))
                throw std::runtime_error("invalid OBJ vertex");
            for (unsigned i = 0; i < 3; ++i)
            {
                result[i] = matrix[i][3];
                for (unsigned k = 0; k < 3; ++k)
                    result[i] += matrix[i][k] * p[k];
                if (!std::isfinite(result[i]))
                    throw std::runtime_error("nonfinite OBJ vertex");
            }
            positions.push_back(result);
        }
        else if (command == "vn")
        {
            Vec source{}, result{};
            if (!(row >> source[0] >> source[1] >> source[2]))
                throw std::runtime_error("invalid OBJ normal");
            // Inverse transpose handles nonuniform scale and reflected imports.
            for (unsigned k = 0; k < 3; ++k)
            {
                const unsigned a = (k + 1) % 3, b = (k + 2) % 3;
                const auto cofactor = cross(Vec{matrix[a][0], matrix[a][1], matrix[a][2]},
                                            Vec{matrix[b][0], matrix[b][1], matrix[b][2]});
                for (unsigned v = 0; v < 3; ++v)
                    result[k] += cofactor[v] * source[v] / determinant;
            }
            normals.push_back(normal(result));
        }
        else if (command == "vt")
        {
            std::array<float, 2> uv{};
            if (!(row >> uv[0] >> uv[1]) || !std::isfinite(uv[0]) || !std::isfinite(uv[1]) ||
                std::abs(uv[0]) > 65504 || std::abs(uv[1]) > 65503)
                throw std::runtime_error("invalid OBJ texture coordinate");
            uv[1] = 1.0f - uv[1]; // OBJ bottom-left origin -> native texture top-left origin.
            texcoords.push_back(uv);
        }
        else if (command == "f")
        {
            std::vector<size_t> face;
            std::vector<std::array<float, 2>> faceUV;
            std::vector<size_t> faceNormals;
            std::string part;
            while (row >> part)
            {
                if (part.starts_with('#'))
                    break;
                const auto slash = part.find('/');
                const auto vertex = part.substr(0, slash);
                size_t consumed = 0;
                const int index = std::stoi(vertex, &consumed);
                const int64_t resolved =
                    index > 0 ? int64_t(index) - 1 : int64_t(positions.size()) + index;
                if (consumed != vertex.size() || !index || resolved < 0 ||
                    resolved >= int64_t(positions.size()))
                    throw std::runtime_error("OBJ face index outside vertices");
                face.push_back(size_t(resolved));
                std::array<float, 2> uv{0.5f, 0.5f};
                if (textured)
                {
                    if (slash == std::string::npos)
                        throw std::runtime_error(
                            "textured weapon requires OBJ UVs on every corner");
                    const auto token =
                        part.substr(slash + 1, part.find('/', slash + 1) - slash - 1);
                    size_t used = 0;
                    const int texIndex = token.empty() ? 0 : std::stoi(token, &used);
                    const int64_t texResolved =
                        texIndex > 0 ? int64_t(texIndex) - 1 : int64_t(texcoords.size()) + texIndex;
                    if (!texIndex || used != token.size() || texResolved < 0 ||
                        texResolved >= int64_t(texcoords.size()))
                        throw std::runtime_error("OBJ texture index outside coordinates");
                    uv = texcoords[size_t(texResolved)];
                }
                faceUV.push_back(uv);
                const auto normalSlash = slash == std::string::npos ? slash : part.find('/', slash + 1);
                size_t normalIndex = SIZE_MAX;
                if (normalSlash != std::string::npos && normalSlash + 1 < part.size())
                {
                    const auto token = part.substr(normalSlash + 1);
                    size_t used = 0;
                    const int normalToken = std::stoi(token, &used);
                    const int64_t normalResolved = normalToken > 0 ? int64_t(normalToken) - 1 : int64_t(normals.size()) + normalToken;
                    if (!normalToken || used != token.size() || normalResolved < 0 || normalResolved >= int64_t(normals.size()))
                        throw std::runtime_error("OBJ normal index outside normals");
                    normalIndex = size_t(normalResolved);
                }
                faceNormals.push_back(normalIndex);
                if (face.size() > 64)
                    throw std::runtime_error("OBJ polygon exceeds 64 vertices");
            }
            if (face.size() < 3)
                throw std::runtime_error("OBJ face has fewer than three corners");
            for (size_t i = 1; i + 1 < face.size(); ++i)
            {
                if (parts.empty() || parts.back().name != group || parts.back().surface.verticies.size() >= 30000)
                {
                    if (parts.size() >= 128)
                        throw std::runtime_error("weapon model exceeds 128 native surfaces");
                    parts.push_back({group, {}, {}});
                }
                auto &meshPart = parts.back();
                auto &surface = meshPart.surface;
                const Vec tri[] = {positions[face[0]], positions[face[i]], positions[face[i + 1]]};
                Vec a{}, b{};
                for (unsigned k = 0; k < 3; ++k)
                {
                    a[k] = tri[1][k] - tri[0][k];
                    b[k] = tri[2][k] - tri[0][k];
                }
                const auto area = cross(a, b);
                // Exported OBJ polygons can repeat/collinearize fan corners.
                // They contribute no surface; an entirely degenerate mesh is rejected below.
                if (area[0] * area[0] + area[1] * area[1] + area[2] * area[2] < 1e-14f)
                    continue;
                auto n = normal(area);
                if (determinant < 0)
                    for (auto &component : n)
                        component = -component;
                const std::array<float, 2> triangleUV[] = {faceUV[0], faceUV[i], faceUV[i + 1]};
                const size_t triangleNormals[] = {faceNormals[0], faceNormals[i], faceNormals[i + 1]};
                const size_t triangleVertices[] = {face[0], face[i], face[i + 1]};
                const float du1 = triangleUV[1][0] - triangleUV[0][0], dv1 = triangleUV[1][1] - triangleUV[0][1];
                const float du2 = triangleUV[2][0] - triangleUV[0][0], dv2 = triangleUV[2][1] - triangleUV[0][1];
                const float uvArea = du1 * dv2 - du2 * dv1;
                Vec uvTangent{}, uvBitangent{};
                if (std::abs(uvArea) > 1e-10f)
                    for (unsigned k = 0; k < 3; ++k)
                    {
                        uvTangent[k] = (a[k] * dv2 - b[k] * dv1) / uvArea;
                        uvBitangent[k] = (b[k] * du1 - a[k] * du2) / uvArea;
                    }
                for (size_t corner = 0; corner < 3; ++corner)
                {
                    const auto &p = tri[corner];
                    if (surface.verticies.size() >= 65535)
                        throw std::runtime_error("OBJ exceeds 65535 triangle corners");
                    dumpsrc::XseVertex vertex;
                    std::copy(p.begin(), p.end(), vertex.xyz);
                    vertex.color = 0xFFFFFFFF;
                    const auto vertexNormal = triangleNormals[corner] == SIZE_MAX ? n : normals[triangleNormals[corner]];
                    Vec tangent = uvTangent;
                    float dot = 0;
                    for (unsigned k = 0; k < 3; ++k)
                        dot += tangent[k] * vertexNormal[k];
                    for (unsigned k = 0; k < 3; ++k)
                        tangent[k] -= vertexNormal[k] * dot;
                    const float lengthSquared = tangent[0]*tangent[0] + tangent[1]*tangent[1] + tangent[2]*tangent[2];
                    tangent = normal(lengthSquared < 1e-12f
                        ? cross(std::abs(vertexNormal[2]) < 0.9f ? Vec{0, 0, 1} : Vec{0, 1, 0}, vertexNormal)
                        : tangent);
                    const auto bitangent = cross(vertexNormal, tangent);
                    const float sign = bitangent[0]*uvBitangent[0] + bitangent[1]*uvBitangent[1] + bitangent[2]*uvBitangent[2];
                    vertex.binormalSign = sign < 0 ? -1.f : 1.f;
                    vertex.normal = packedVector(vertexNormal);
                    vertex.tangent = packedVector(tangent);
                    // The intermediate XSE uses U in the high half, V in the low half.
                    vertex.texCoord = uint32_t(xsurf_conv::floatToHalf(triangleUV[corner][0]))
                                          << 16 |
                                      xsurf_conv::floatToHalf(triangleUV[corner][1]);
                    surface.triIndices.push_back(uint16_t(surface.verticies.size()));
                    surface.verticies.push_back(vertex);
                    Weights weights{{bone, 1.f}};
                    if (const auto found = partBones.find(group); found != partBones.end())
                        weights = {{found->second, 1.f}};
                    else if (!vertexWeights.empty())
                    {
                        if (triangleVertices[corner] >= vertexWeights.size())
                            throw std::runtime_error("OBJ vertex has no skin weights");
                        weights = vertexWeights[triangleVertices[corner]];
                    }
                    for (const auto &[index, weight] : weights)
                        surface.partBits[index / 32] |= static_cast<int32_t>(0x80000000u >> (index % 32));
                    meshPart.weights.push_back(std::move(weights));
                    for (unsigned k = 0; k < 3; ++k)
                    {
                        minimum[k] = std::min(minimum[k], p[k]);
                        maximum[k] = std::max(maximum[k], p[k]);
                    }
                }
                // Replay's stock model indices are clockwise relative to the outward normal;
                // OBJ is counterclockwise. Preserve vertex/UV associations and outward normals.
                // A reflected authoring transform has already reversed the triangle order.
                if (determinant > 0)
                    std::swap(surface.triIndices[surface.triIndices.size() - 2],
                              surface.triIndices.back());
            }
        }
    }
    if (!vertexWeights.empty() && vertexWeights.size() != positions.size())
        throw std::runtime_error("skin weight count does not match OBJ vertices");
    dumpsrc::XseFile xse;
    xse.loaded = true;
    xse.name = name + "_lod0";
    for (auto &part : parts)
    {
        auto &surface = part.surface;
        if (surface.verticies.empty())
            continue;
        surface.vertCount = uint32_t(surface.verticies.size());
        surface.triCount = uint32_t(surface.triIndices.size() / 3);
        const unsigned rigid = part.weights.front().front().first;
        const bool isRigid = std::all_of(part.weights.begin(), part.weights.end(), [&](const Weights &w) {
            return w.size() == 1 && w[0].first == rigid;
        });
        if (isRigid)
        {
            surface.vertListCount = 1;
            surface.rigidVertLists.push_back({uint16_t(rigid * 64), uint16_t(surface.vertCount), 0, uint16_t(surface.triCount)});
        }
        else
        {
            surface.deformed = 1;
            std::vector<dumpsrc::XseVertex> sorted;
            std::vector<uint16_t> indices(surface.vertCount);
            for (size_t tier = 1; tier <= 4; ++tier)
                for (size_t v = 0; v < part.weights.size(); ++v)
                {
                    const auto &weights = part.weights[v];
                    if (weights.size() != tier)
                        continue;
                    indices[v] = static_cast<uint16_t>(sorted.size());
                    sorted.push_back(surface.verticies[v]);
                    ++surface.vertBlendCounts[tier - 1];
                    surface.vertsBlend.push_back(uint16_t(weights[0].first * 64));
                    uint32_t remaining = UINT16_MAX;
                    for (size_t k = 1; k < tier; ++k)
                    {
                        const auto weight = std::min(remaining, uint32_t(std::lround(weights[k].second * UINT16_MAX)));
                        remaining -= weight;
                        surface.vertsBlend.push_back(uint16_t(weights[k].first * 64));
                        surface.vertsBlend.push_back(uint16_t(weight));
                    }
                }
            for (auto &index : surface.triIndices)
                index = indices[index];
            surface.verticies = std::move(sorted);
        }
        for (unsigned k = 0; k < 6; ++k)
            xse.modelSurfPartBits[k] |= surface.partBits[k];
        xse.surfaces.push_back(std::move(surface));
    }
    if (xse.surfaces.empty())
        throw std::runtime_error("OBJ has no triangles or only degenerate OBJ triangles");
    model.numsurfs = static_cast<uint16_t>(xse.surfaces.size());
    model.materials.assign(xse.surfaces.size(), text(j.at("material")));
    model.himipRadiusInvSq.assign(xse.surfaces.size(), 0);
    const auto converted = conv_xsurf::convert(xse);
    if (!converted.ok)
        throw std::runtime_error("failed to convert melee geometry");
    for (unsigned k = 0; k < 3; ++k)
    {
        model.boundsMid[k] = (minimum[k] + maximum[k]) * 0.5f;
        model.boundsHalf[k] = (maximum[k] - minimum[k]) * 0.5f;
        model.radius += std::pow(std::max(std::abs(minimum[k]), std::abs(maximum[k])), 2.0f);
    }
    // Conservative per-bone bounds cover every vertex influenced by that bone.
    // Keep bounds in the bone's bind space, as native animated culling expects.
    for (unsigned index = 0; index < model.numBones; ++index)
    {
        if (!(xse.modelSurfPartBits[index / 32] & (0x80000000u >> (index % 32))))
            continue;
        const auto &pose = model.skeleton.baseMat[index];
        Vec lo{1e20f,1e20f,1e20f}, hi{-1e20f,-1e20f,-1e20f};
        for (unsigned corner = 0; corner < 8; ++corner)
        {
            Vec p{}, q{-pose.quat[0],-pose.quat[1],-pose.quat[2]};
            for (unsigned k = 0; k < 3; ++k)
                p[k] = (corner & (1u << k) ? maximum[k] : minimum[k]) - pose.trans[k];
            const auto t = cross(q, p), u = cross(q, t);
            for (unsigned k = 0; k < 3; ++k)
            {
                p[k] += 2 * (pose.quat[3] * t[k] + u[k]);
                lo[k] = std::min(lo[k], p[k]);
                hi[k] = std::max(hi[k], p[k]);
            }
        }
        auto &bounds = model.skeleton.boneInfo[index];
        for (unsigned k = 0; k < 3; ++k)
        {
            bounds.midPoint[k] = (lo[k] + hi[k]) * .5f;
            bounds.halfSize[k] = (hi[k] - lo[k]) * .5f;
            bounds.radiusSquared += std::pow(std::max(std::abs(lo[k]), std::abs(hi[k])), 2.f);
        }
    }
    model.radius = std::sqrt(model.radius);
    convert::xmodel::Iw8LodInfo lod;
    lod.dist = 1000000;
    lod.numsurfs = model.numsurfs;
    lod.surfsName = xse.name;
    std::copy(std::begin(converted.partBits), std::end(converted.partBits), lod.partBits.begin());
    model.lods.push_back(lod);
    iw8xs_dump::writeXModelSurfs(w, xse.name, converted);
    writeXModel(w, model);
    zt::info("weapon model '%s': %u triangles, %u surfaces, %u bones", name.c_str(), converted.totalTris,
             model.numsurfs, model.numBones);
}
} // namespace

bool buildWeapon(const std::string &input, const std::string &outputDirectory)
{
    const fs::path source = fs::absolute(input);
    const auto config = readJson(source);
    if (config.at("format") != "replay-melee-v1")
        throw std::runtime_error("unsupported weapon input format");
    const std::string name = text(config.at("weapon"));
    if (name != "iw8_knife_mp")
        throw std::runtime_error("first melee profile supports the base iw8_knife_mp slot only");
    const std::string zone = "custom_weapons";
    const auto reference = readJson(source.parent_path() / text(config.at("reference")));
    if (reference.at("format") != "replay-melee-reference-v1" ||
        reference.at("source_weapon") != name)
        throw std::runtime_error("melee reference must be the stock Replay base knife");
    auto root = reference.at("root");
    if (unhex(root.at("data")).size() != 0x270)
        throw std::runtime_error("incorrect WeaponCompleteDef size");
    bool definitionFound = false;
    for (const auto &f : root.at("fixups"))
        if (f.at("offset") == 8 && f.at("kind") == "record" && unhex(f.at("data")).size() == 0x14B0)
            definitionFound = true;
    if (!definitionFound)
        throw std::runtime_error("incorrect or missing Replay WeaponDef");
    const Replacements replacements{
        {{9, "weapon_vm_me_soscar_knife"}, "custom/knife_vm"},
        {{9, "weapon_wm_me_soscar_knife"}, "custom/knife_wm"}};
    ZoneWriter writer;
    std::set<std::pair<unsigned, std::string>> references;
    prepareRecord(writer, root, references, replacements);
    for (const char *kind : {"view_model", "world_model"})
        references.emplace(11, text(config.at(kind).at("material")));
    for (const auto &[type, dependencyName] : references)
        dependency(writer, type, dependencyName);
    const fs::path mesh = source.parent_path() / text(config.at("model"));
    addModel(writer, mesh, config.at("view_model"), "custom/knife_vm");
    addModel(writer, mesh, config.at("world_model"), "custom/knife_wm");
    writer.add(static_cast<IW8_XAssetType>(43), name,
               [root, owner = AssetKey{43, name}](ZoneWriter &w) {
                   emitRecord(w, root, true, &owner);
               });
    writer.build();
    zt::Iw8WriteParams params{};
    for (unsigned i = 0; i < 11; ++i)
        params.blockSize[i] = writer.blockSize(i);
    params.totalDecompressed = writer.totalDecompressed();
    params.calcSize = writer.calcSize();
    const fs::path output =
        outputDirectory.empty() ? source.parent_path() / "output" : fs::path(outputDirectory);
    fs::create_directories(output);
    const auto file = output / (zone + ".ff");
    if (!zt::iw8_write(file.string(), writer.body(), params))
        return false;
    zt::info("weapon package '%s': %zu native assets; base knife replacement",
             file.string().c_str(), writer.assetCount());
    return true;
}

namespace
{
std::string hex(const std::vector<uint8_t> &bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto byte : bytes)
    {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

bool editable(const std::string &field)
{
    // Sizes, counts and runtime indices must continue to describe the imported
    // arrays. They are exposed in --list-fields but cannot be changed by --set.
    return field.find("attachments[") == std::string::npos &&
           field.find(".num") == std::string::npos &&
           (field.find("Count") == std::string::npos || field.ends_with(".burstCount") ||
            field.ends_with(".shotCount") || field.ends_with(".lowAmmoWarningCount")) &&
           field.find("Index") == std::string::npos && field.find(".unknown") == std::string::npos;
}

void listFields(const Json &record)
{
    for (const auto &field : record.value("fields", Json::array()))
        std::printf("%s\t%s\t+0x%zX\t%s\t%s\n", field.at("field").get<std::string>().c_str(),
                    field.at("type").get<std::string>().c_str(), field.at("offset").get<size_t>(),
                    field.at("value").dump().c_str(),
                    editable(field.at("field")) ? "editable" : "layout/runtime");
    for (const auto &f : record.at("fixups"))
        if (f.at("kind") == "record")
            listFields(f);
        else
            std::printf("%s\t%s\t+0x%zX\t%s\n", f.at("field").get<std::string>().c_str(),
                        f.at("kind").get<std::string>().c_str(), f.at("offset").get<size_t>(),
                        f.value("text", f.value("name", std::string())).c_str());
}

bool setField(Json &record, const std::string &path, const std::string &value)
{
    for (auto &f : record["fields"])
    {
        if (f.at("field") != path)
            continue;
        if (!editable(path))
            throw std::runtime_error("field describes an array or runtime index: " + path);
        auto bytes = unhex(record.at("data"));
        const size_t offset = f.at("offset"), size = f.at("size");
        if (offset > bytes.size() || size > bytes.size() - offset)
            throw std::runtime_error("field outside reference record: " + path);
        const auto type = f.at("type").get<std::string>();
        size_t end = 0;
        if (type == "float" || type == "double")
        {
            const double parsed = std::stod(value, &end);
            if (!std::isfinite(parsed) ||
                (type == "float" && std::abs(parsed) > std::numeric_limits<float>::max()))
                throw std::runtime_error("nonfinite/out-of-range floating point field: " + path);
            if (type == "float" && size == 4)
                put(bytes, offset, float(parsed));
            else if (type == "double" && size == 8)
                put(bytes, offset, parsed);
            else
                throw std::runtime_error("invalid floating point field width: " + path);
            f["value"] = parsed;
        }
        else
        {
            const bool boolean = type == "bool";
            const std::string number = boolean && value == "true"    ? "1"
                                       : boolean && value == "false" ? "0"
                                                                     : value;
            const bool isUnsigned = type.find("unsigned") != std::string::npos || boolean;
            if (!size || size > 8)
                throw std::runtime_error("invalid integer field width: " + path);
            uint64_t raw = 0;
            if (isUnsigned)
            {
                if (number.starts_with('-'))
                    throw std::runtime_error("negative unsigned field: " + path);
                raw = std::stoull(number, &end, 0);
                if ((size < 8 && raw >= (uint64_t(1) << (size * 8))) || (boolean && raw > 1))
                    throw std::runtime_error("integer field out of range: " + path);
                f["value"] = raw;
            }
            else
            {
                const int64_t parsed = std::stoll(number, &end, 0);
                if (size < 8 && (parsed < -(int64_t(1) << (size * 8 - 1)) ||
                                 parsed >= (int64_t(1) << (size * 8 - 1))))
                    throw std::runtime_error("integer field out of range: " + path);
                raw = uint64_t(parsed);
                f["value"] = parsed;
            }
            if (end != number.size())
                throw std::runtime_error("invalid numeric field: " + path);
            for (size_t i = 0; i < size; ++i)
                bytes[offset + i] = uint8_t(raw >> (i * 8));
            end = value.size();
        }
        if (end != value.size())
            throw std::runtime_error("invalid numeric field: " + path);
        record["data"] = hex(bytes);
        return true;
    }
    for (auto &f : record["fixups"])
    {
        if (f.at("kind") == "record" && setField(f, path, value))
            return true;
        if (f.at("field") == path && (f.at("kind") == "string" || f.at("kind") == "script"))
        {
            f["text"] = text(Json(value));
            return true;
        }
    }
    return false;
}

void setRequired(Json &record, const std::string &path, const std::string &value)
{
    if (!setField(record, path, value))
        throw std::runtime_error("field not present in reference: " + path);
}

void validateStats(const Json &record)
{
    const std::map<std::string, std::pair<double, double>> limits{
        {"weapon.iClipSize", {0, 1000}},
        {"weapon.weapDef[0].iFireTime", {0, 60000}},
        {"weapon.weapDef[0].iFireTimeAkimbo", {0, 60000}},
        {"weapon.weapDef[0].iStartAmmo", {0, 100000}},
        {"weapon.weapDef[0].iMaxAmmo", {0, 100000}},
        {"weapon.weapDef[0].damageInfo.damageData[0].damage", {0, 100000}},
        {"weapon.weapDef[0].damageInfo.damageData[0].minDamage", {0, 100000}}};
    for (const auto &field : record.value("fields", Json::array()))
        if (const auto it = limits.find(field.at("field")); it != limits.end())
        {
            const auto value = field.at("value").get<double>();
            if (value < it->second.first || value > it->second.second)
                throw std::runtime_error("weapon stat out of supported range: " + it->first);
        }
    for (const auto &f : record.at("fixups"))
        if (f.at("kind") == "record")
            validateStats(f);
}

void erasePointer(Json &record, size_t offset)
{
    auto &fixups = record["fixups"];
    for (auto it = fixups.begin(); it != fixups.end();)
        if (it->at("offset") == offset)
            it = fixups.erase(it);
        else
            ++it;
    auto bytes = unhex(record.at("data"));
    put(bytes, offset, uint64_t(0));
    record["data"] = hex(bytes);
}

void removeAttachments(Json &root)
{
    auto bytes = unhex(root.at("data"));
    for (size_t slot = 0; slot < 14; ++slot)
    {
        put(bytes, 40 + slot * 16, uint32_t(0));
        erasePointer(root, 48 + slot * 16);
    }
    root["data"] = hex(bytes);
    for (auto &f : root["fields"])
        if (f.at("field").get<std::string>().starts_with("weapon.attachments["))
            f["value"] = 0;
    for (auto &def : root["fixups"])
    {
        if (def.at("offset") != 8)
            continue;
        auto data = unhex(def.at("data"));
        for (size_t count : {size_t(4648), size_t(4784), size_t(4800)})
            put(data, count, uint32_t(0));
        def["data"] = hex(data);
        for (size_t pointer : {size_t(4656), size_t(4792), size_t(4808)})
            erasePointer(def, pointer);
        for (auto &f : def["fields"])
            if (f.at("field") == "weapon.weapDef[0].numAnimOverrides" ||
                f.at("field") == "weapon.weapDef[0].numSfxOverrides" ||
                f.at("field") == "weapon.weapDef[0].numVfxOverrides")
                f["value"] = 0;
    }
}

size_t replaceAttachmentModels(Json &record, const std::string &viewName,
                               const std::string &worldName, unsigned depth = 0)
{
    if (depth > 16)
        throw std::runtime_error("attachment model graph is too deep");
    size_t count = 0;
    for (auto &fixup : record["fixups"])
    {
        if (fixup.at("kind") == "record")
        {
            count += replaceAttachmentModels(fixup, viewName, worldName, depth + 1);
            continue;
        }
        if (fixup.at("kind") != "asset" || fixup.at("asset_type") != 9)
            continue;
        auto field = fixup.value("field", std::string());
        std::transform(field.begin(), field.end(), field.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool view = field.find("viewmodelvariations") != std::string::npos;
        const bool world = field.find("worldmodelvariations") != std::string::npos;
        if (!view && !world)
            continue;
        fixup["name"] = world ? worldName : viewName;
        ++count;
    }
    return count;
}

void setAssetReference(Json &record, size_t offset, const std::string &field, unsigned type,
                       const std::string &name)
{
    for (auto &fixup : record["fixups"])
        if (fixup.value("field", std::string()) == field)
        {
            if (fixup.at("kind") != "asset" || fixup.at("offset") != offset ||
                fixup.at("asset_type") != type || fixup.at("size") != dependencySize(type))
                throw std::runtime_error("sound bank field has an unexpected Replay layout");
            fixup["name"] = name;
            return;
        }
    auto bytes = unhex(record.at("data"));
    if (offset > bytes.size() || 8 > bytes.size() - offset ||
        std::any_of(bytes.begin() + offset, bytes.begin() + offset + 8, [](uint8_t byte) { return byte != 0; }))
        throw std::runtime_error("sound bank pointer is nonzero without an asset relocation");
    record["fixups"].push_back(Json{{"offset", offset}, {"kind", "asset"}, {"field", field},
                                      {"asset_type", type}, {"size", dependencySize(type)},
                                      {"name", name}});
}

Json orderOwnedAssets(const Json &assets, std::set<AssetKey> &forwardAssets)
{
    std::map<AssetKey, size_t> indices;
    for (size_t i = 0; i < assets.size(); ++i)
        indices[{assets[i].at("pool").get<unsigned>(), text(assets[i].at("name"))}] = i;
    std::vector<unsigned char> state(assets.size());
    Json ordered = Json::array();
    std::function<void(size_t)> visit;
    std::function<void(const Json &, size_t)> references = [&](const Json &record, size_t owner) {
        for (const auto &f : record.at("fixups"))
            if (f.at("kind") == "record")
                references(f, owner);
            else if (f.at("kind") == "asset")
                if (auto found = indices.find({f.at("asset_type").get<unsigned>(), text(f.at("name"))});
                    found != indices.end())
                {
                    if (found->second != owner)
                        visit(found->second);
                }
    };
    visit = [&](size_t index) {
        if (state[index] == 2)
            return;
        if (state[index] == 1)
        {
            // Replay DB_LinkXAssetEntry (11AC760) registers comma references as
            // stable default pool entries, then promotes the entry when its full
            // definition loads. Reserve that native handle before a cycle. A
            // TEMP header alias would be invalid after the loader reuses TEMP.
            forwardAssets.emplace(assets[index].at("pool").get<unsigned>(), text(assets[index].at("name")));
            return;
        }
        state[index] = 1;
        references(assets[index].at("root"), index);
        state[index] = 2;
        ordered.push_back(assets[index]);
    };
    for (size_t i = 0; i < assets.size(); ++i)
        visit(i);
    return ordered;
}

} // namespace

int weaponMain(int argc, char **argv)
{
    std::map<std::string, std::string> options;
    std::vector<std::pair<std::string, std::string>> edits;
    std::string legacy;
    bool list = false;
    for (int i = 2; i < argc; ++i)
    {
        const std::string flag = argv[i];
        if (flag == "--help" || flag == "-h")
        {
            std::printf(
                "build-weapon --reference <reference.json> --name <cw_name>\n"
                "  --display-name <name> --model <mesh.obj> --rig <rig.json> -o <output>\n"
                "  [--material <material.json>] (owned single-UV lit material and textures)\n"
                "  [--rpm <rounds/min>] [--mag-size <rounds>] [--start-ammo <rounds>]\n"
                "  [--max-ammo <rounds>] [--damage <points>] [--min-damage <points>]\n"
                "  [--fire-sound <NPC alias>] [--fire-sound-player <player alias>]\n"
                "  [--sound-bank <resident/transient bank asset name>]\n"
                "  [--set <exact.field=value>] [--list-fields]\n"
                "  [--project <build.json>] (editor snapshot with native owned assets)\n"
                "  [--loadout-slot <61..254>] (default 61; unique per installed gun)\n"
                "Names must begin iw8_cw_; emits <name>.ff and <name>.weapon.json.\n"
                "Project input supports Replay weapon categories and attachment graphs.\n");
            return 0;
        }
        if (flag == "--list-fields")
        {
            list = true;
            continue;
        }
        if (!flag.starts_with('-') && legacy.empty())
        {
            legacy = flag;
            continue;
        }
        if (i + 1 >= argc)
            throw std::runtime_error("missing value for " + flag);
        const std::string value = argv[++i];
        if (flag == "--set")
        {
            const auto split = value.find('=');
            if (split == std::string::npos || !split)
                throw std::runtime_error("--set expects exact.field=value");
            edits.emplace_back(value.substr(0, split), value.substr(split + 1));
        }
        else if (flag == "--project" || flag == "--reference" || flag == "--name" || flag == "--display-name" ||
                 flag == "--model" || flag == "--rig" || flag == "--material" || flag == "-o" ||
                 flag == "--rpm" || flag == "--loadout-slot" || flag == "--mag-size" ||
                 flag == "--start-ammo" || flag == "--max-ammo" || flag == "--damage" ||
                 flag == "--min-damage" || flag == "--fire-sound" ||
                 flag == "--fire-sound-player" || flag == "--sound-bank")
        {
            if (!options.emplace(flag, value).second)
                throw std::runtime_error("duplicate weapon option: " + flag);
        }
        else
            throw std::runtime_error("unknown weapon option: " + flag);
    }
    if (!legacy.empty())
    {
        if (list || !edits.empty() || options.size() > (options.contains("-o") ? 1 : 0))
            throw std::runtime_error(
                "legacy melee manifest cannot be combined with standalone weapon flags");
        return buildWeapon(legacy, options["-o"]) ? 0 : 1;
    }
    Json project;
    if (options.contains("--project"))
    {
        const auto path = fs::absolute(options.at("--project"));
        project = readJson(path);
        if (project.at("format") != "replay-weapon-build-v1")
            throw std::runtime_error("expected replay-weapon-build-v1 project snapshot");
        for (const auto &[key, flag] : std::map<std::string, std::string>{
                 {"reference", "--reference"}, {"model", "--model"}, {"rig", "--rig"},
                 {"material", "--material"}, {"name", "--name"}, {"display_name", "--display-name"}})
            if (project.contains(key))
            {
                if (options.contains(flag))
                    throw std::runtime_error("project owns option " + flag);
                auto value = text(project.at(key));
                if (key == "reference" || key == "model" || key == "rig" || key == "material")
                    value = (path.parent_path() / fs::path(value)).lexically_normal().string();
                options[flag] = value;
            }
        options["--loadout-slot"] = std::to_string(project.at("loadout_slot").get<unsigned>());
    }
    const auto required = [&](const char *flag) -> std::string {
        if (!options.contains(flag) || options.at(flag).empty())
            throw std::runtime_error(std::string("missing weapon option: ") + flag);
        return options.at(flag);
    };
    const auto reference = readJson(required("--reference"));
    if (reference.at("format") != "replay-weapon-reference-v1")
        throw std::runtime_error(
            "expected replay-weapon-reference-v1; use prepare_weapon_reference.py");
    auto root = reference.at("root"), sfx = reference.at("sfx");
    if (sfx.is_null())
        sfx = Json{{"alignment", 8}, {"data", std::string(176 * 2, '0')},
                   {"fields", Json::array()}, {"fixups", Json::array({
                       Json{{"offset", 0}, {"kind", "string"}, {"field", "sfx.name"}, {"text", ""}}})}};
    if (unhex(root.at("data")).size() != 624 || unhex(sfx.at("data")).size() != 176)
        throw std::runtime_error("incorrect Replay weapon/SFX root sizes");
    if (list)
    {
        listFields(root);
        listFields(sfx);
        return 0;
    }
    const auto base = required("--name");
    if (!base.starts_with("iw8_cw_") || base.size() > 48 || base.size() < 8 ||
        base.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
        base.ends_with("_mp"))
        throw std::runtime_error(
            "custom base name must be iw8_cw_<lowercase_name>, without _mp, at most 48 characters");
    const auto name = base + "_mp", sfxName = name + "_sfx";
    size_t slotEnd = 0;
    const std::string slotText =
        options.contains("--loadout-slot") ? options.at("--loadout-slot") : "61";
    const auto slot = std::stoul(slotText, &slotEnd);
    if (slotEnd != slotText.size() || slot < 61 || slot > 254)
        throw std::runtime_error(
            "--loadout-slot must be between 61 and 254; never reuse a saved gun's slot");
    const auto display = text(Json(required("--display-name")));
    auto owned = project.is_null() ? Json::array() : project.value("owned_assets", Json::array());
    const bool customModels = options.contains("--model") || options.contains("--rig");
    const bool customAttachmentModels = std::any_of(owned.begin(), owned.end(), [](const Json &asset) {
        return asset.value("pool", 0u) == 42 && asset.contains("geometry");
    });
    const bool customGeometry = customModels || customAttachmentModels;
    const bool attachments = !project.is_null() && project.value("attachments", false);
    auto rig = customModels ? readJson(required("--rig")) : Json();
    if (customModels && rig.at("format") != "replay-weapon-rig-v1")
        throw std::runtime_error("expected replay-weapon-rig-v1");
    replayrender::Mesh material;
    if (customGeometry && options.contains("--material"))
    {
        material = weaponMaterial(options.at("--material"), base);
        if (customModels)
            for (const char *kind : {"view_model", "world_model"})
                rig[kind]["material"] = material.material;
    }
    else if (customModels)
        for (const char *kind : {"view_model", "world_model"})
        {
            const auto borrowed = text(rig.at(kind).at("material"));
            if (borrowed.starts_with("mo/") || borrowed.starts_with("mco/") ||
                borrowed.starts_with("m2o/") || borrowed.starts_with("m2co/"))
                throw std::runtime_error(
                    "rig material requires an unsupported vertex layout; supply --material");
        }
    if (!attachments)
        removeAttachments(root);
    setRequired(root, "weapon.szInternalName", name);
    setRequired(root, "weapon.szDisplayName", "CUSTOM_WEAPON/" + base);
    // This project's base variant is emitted below; do not inherit stock loot identity.
    erasePointer(root, 24);
    setRequired(sfx, "sfx.name", sfxName);
    if (options.contains("--sound-bank"))
    {
        bool found = false;
        for (auto &f : sfx["fixups"])
            if (f.at("field") == "sfx.detailSoundBankPlayer" && f.at("kind") == "asset")
            {
                f["name"] = text(Json(options.at("--sound-bank")));
                found = true;
            }
        if (!found)
            throw std::runtime_error("reference has no detailSoundBankPlayer dependency");
    }
    const std::map<std::string, std::string> friendly{
        {"--mag-size", "weapon.iClipSize"},
        {"--start-ammo", "weapon.weapDef[0].iStartAmmo"},
        {"--max-ammo", "weapon.weapDef[0].iMaxAmmo"},
        {"--damage", "weapon.weapDef[0].damageInfo.damageData[0].damage"},
        {"--min-damage", "weapon.weapDef[0].damageInfo.damageData[0].minDamage"},
        {"--fire-sound", "sfx.sounds[0].fireSound.name"},
        {"--fire-sound-player", "sfx.sounds[0].fireSoundPlayer.name"}};
    if (options.contains("--rpm"))
    {
        size_t end = 0;
        const auto rpm = std::stod(options.at("--rpm"), &end);
        if (end != options.at("--rpm").size() || !std::isfinite(rpm) || rpm < 1 || rpm > 60000)
            throw std::runtime_error("--rpm must be between 1 and 60000");
        const auto interval = std::to_string(std::lround(60000.0 / rpm));
        setRequired(root, "weapon.weapDef[0].iFireTime", interval);
        setRequired(root, "weapon.weapDef[0].iFireTimeAkimbo", interval);
    }
    for (const auto &[flag, path] : friendly)
        if (options.contains(flag))
            setRequired(path.starts_with("sfx.") ? sfx : root, path, options.at(flag));
    // First/last/akimbo alternatives must not switch back to the reference
    // pistol on an empty magazine. Explicit --set options below can add tails.
    for (auto &sounds : sfx["fixups"])
        if (sounds.at("kind") == "record" && sounds.at("offset") == 8)
            for (auto &sound : sounds["fixups"])
            {
                const auto field = sound.at("field").get<std::string>();
                if (sound.at("kind") != "string" || field.find(".fire") == std::string::npos)
                    continue;
                const char *flag = field.find("Player") != std::string::npos ? "--fire-sound-player"
                                                                             : "--fire-sound";
                if (options.contains(flag))
                    sound["text"] =
                        field.find("Atmosphere") != std::string::npos ? "" : options.at(flag);
            }
    for (const auto &[path, value] : edits)
    {
        if (path == "weapon.szInternalName" || path == "sfx.name" || path == "weapon.szLootTable")
            throw std::runtime_error("identity and blueprint fields are managed by the compiler");
        setRequired(path.starts_with("sfx.") ? sfx : root, path, value);
    }
    validateStats(root);
    if (customModels)
    {
        // The base meshes are resident in this zone. Modular attachment assets
        // retain their own native streaming classification when present.
        setRequired(root, "weapon.weapDef[0].transientBaseViewFlags", "0");
        setRequired(root, "weapon.weapDef[0].transientBaseWorldFlags", "0");
        if (!attachments)
            setRequired(root, "weapon.weapDef[0].hasAnyTransientModels", "false");
    }
    // Model substitutions include streaming fallbacks: a custom base cannot
    // request the stock modular receiver or its transient placeholder mesh.
    Replacements replacements{
        {{78, reference.at("source_sfx").get<std::string>()}, sfxName}};
    if (customModels)
    {
        for (const auto &model : rig.at("view_model").at("replace"))
            replacements[{9, text(model)}] = base + "/vm";
        for (const auto &model : rig.at("world_model").at("replace"))
            replacements[{9, text(model)}] = base + "/wm";
    }
    const auto animations = project.is_null() ? Json::array() : project.value("animations", Json::array());
    const auto sounds = project.is_null() ? Json::array() : project.value("sounds", Json::array());
    if (!animations.is_array() || animations.size() > 128)
        throw std::runtime_error("invalid custom animation collection");
    std::set<std::string> animationNames;
    for (const auto &animation : animations)
    {
        const auto animationName = text(animation.at("asset"));
        if (animationName.empty() || animationName.starts_with(',') || animationName.size() > 120 ||
            animationName.find("..") != std::string::npos ||
            !animationNames.emplace(animationName).second)
            throw std::runtime_error("invalid or duplicate custom animation name");
    }
    std::string customBankName;
    std::vector<uint8_t> customBankBytes;
    if (!sounds.is_array() || sounds.size() > 512)
        throw std::runtime_error("invalid custom sound collection");
    if (!sounds.empty())
    {
        if (!project.contains("sound_bank"))
            throw std::runtime_error("custom sounds require a resident SAB path");
        customBankName = base + ".all";
        customBankBytes = readBinary(fs::path(text(project.at("sound_bank"))));
        std::set<std::string> aliases;
        for (const auto &sound : sounds)
        {
            const auto alias = text(sound.at("alias"));
            if (alias.empty() || alias.size() > 120 || alias.starts_with(',') ||
                alias.find("..") != std::string::npos || !aliases.emplace(alias).second)
                throw std::runtime_error("invalid or duplicate custom sound alias");
            if (sound.contains("event") && !text(sound.at("event")).empty())
                setRequired(sfx, text(sound.at("event")), alias);
        }
        setAssetReference(sfx, 32, "sfx.detailSoundBankPlayer", 22, customBankName);
    }
    std::set<std::pair<unsigned, std::string>> ownedNames;
    for (auto &asset : owned)
    {
        const auto assetName = text(asset.at("name"));
        const unsigned type = asset.at("pool");
        if ((type != 42 && type != 77 && type != 78 && type != 79) ||
            unhex(asset.at("root").at("data")).size() != dependencySize(type) ||
            assetName.empty() || assetName.starts_with(',') ||
            !ownedNames.emplace(type, assetName).second)
            throw std::runtime_error("invalid or duplicate owned weapon asset");
        // Own definitions are registered below, rather than emitted as comma stubs.
        replacements[{type, assetName}] = assetName;
        if (asset.contains("geometry"))
        {
            if (type != 42 || assetName.size() > 100)
                throw std::runtime_error("custom geometry is only supported on owned attachment assets");
            auto &geometry = asset["geometry"];
            const auto modelPath = fs::path(text(geometry.at("model")));
            if (!fs::is_regular_file(modelPath))
                throw std::runtime_error("custom attachment OBJ is missing: " + modelPath.string());
            if (geometry.at("rig").at("format") != "replay-weapon-rig-v1")
                throw std::runtime_error("custom attachment has an invalid rig");
            const auto viewName = assetName + "/custom_vm";
            const auto worldName = assetName + "/custom_wm";
            if (!replaceAttachmentModels(asset["root"], viewName, worldName))
                throw std::runtime_error("cloned attachment has no XModel references to replace");
            replacements[{9, viewName}] = viewName;
            replacements[{9, worldName}] = worldName;
        }
    }
    std::set<AssetKey> forwardAssets;
    owned = orderOwnedAssets(owned, forwardAssets);
    ZoneWriter writer;
    std::set<std::pair<unsigned, std::string>> references;
    for (const auto &key : forwardAssets)
        if (key.first == 42)
            references.insert(key);
    prepareRecord(writer, root, references, replacements);
    references.emplace(78, sfxName); // full SFX package has the common-zone lifetime
    for (auto &asset : owned)
        if (asset.at("pool") == 42)
            prepareRecord(writer, asset["root"], references, replacements);
        else
            references.emplace(asset.at("pool").get<unsigned>(), text(asset.at("name")));
    if (customGeometry && !material.material.empty())
        references.emplace(11, material.material);
    if (customModels && material.material.empty())
        for (const char *kind : {"view_model", "world_model"})
            references.emplace(11, text(rig.at(kind).at("material")));
    if (customAttachmentModels && material.material.empty())
        for (const auto &asset : owned)
            if (asset.contains("geometry"))
                for (const char *kind : {"view_model", "world_model"})
                    references.emplace(11, text(asset.at("geometry").at("rig").at(kind).at("material")));
    for (const auto &[type, dep] : references)
        dependency(writer, type, dep);
    if (customModels)
    {
        addModel(writer, required("--model"), rig.at("view_model"), base + "/vm",
                 !material.material.empty());
        addModel(writer, required("--model"), rig.at("world_model"), base + "/wm",
                 !material.material.empty());
    }
    for (const auto &asset : owned)
        if (asset.contains("geometry"))
        {
            const auto &geometry = asset.at("geometry");
            for (const char *kind : {"view_model", "world_model"})
            {
                auto attachmentRig = geometry.at("rig").at(kind);
                attachmentRig["transform"] = geometry.at(kind).at("matrix");
                if (!material.material.empty())
                    attachmentRig["material"] = material.material;
                addModel(writer, fs::path(text(geometry.at("model"))), attachmentRig,
                         text(asset.at("name")) + (std::string(kind) == "view_model" ? "/custom_vm" : "/custom_wm"),
                         !material.material.empty());
            }
        }
    for (const auto &asset : owned)
        if (asset.at("pool") == 42)
        {
            const AssetKey owner{42, text(asset.at("name"))};
            writer.add(static_cast<IW8_XAssetType>(42), owner.second,
                       [data = asset.at("root"), owner](ZoneWriter &w) {
                           emitRecord(w, data, true, &owner);
                       });
        }
    writer.add(static_cast<IW8_XAssetType>(43), name,
               [root, owner = AssetKey{43, name}](ZoneWriter &w) {
                   emitRecord(w, root, true, &owner);
               });
    // Loading a WeaponCompleteDef does not assign a runtime weapon index.
    // Replay enumerates weapon NetConstStrings when registering client/server
    // weapons. Use the stock global-stream weapon type/source, with a separate
    // asset name that sorts after the stock common lists (Replay 10EFE40).
    const auto weaponNames = "ncs_wep_zz_" + base;
    writer.add(static_cast<IW8_XAssetType>(61), weaponNames, [weaponNames, name](ZoneWriter &w) {
        std::vector<uint8_t> header(32);
        put(header, 0, PTR_FOLLOWS);
        put(header, 8, uint32_t(14)); // NETCONSTSTRINGTYPE_WEAPON in Replay
        put(header, 12, uint32_t(0)); // common/global source
        put(header, 20, uint32_t(1));
        put(header, 24, PTR_FOLLOWS);
        w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
        w.align(7);
        w.write(header.data(), header.size());
        w.pushStream(XFILE_BLOCK_VIRTUAL);
        w.writeStr(weaponNames);
        w.align(7);
        w.writeT(PTR_FOLLOWS);
        w.writeStr(name);
        w.popStream();
        w.popStream();
    });
    // Even an attachment-free base needs a progression table: WeaponLevelBar
    // reads it while constructing the selection button. Level zero is displayed
    // as level one; empty unlock columns grant no attachments or cosmetics.
    const auto tableBase = "mp/gunsmith/" + base.substr(4);
    std::vector<std::string> variant(26);
    variant[0] = "0";
    variant[1] = base + "_variant_0";
    variant[2] = base;
    variant[3] = name;
    variant[17] = "CUSTOM_WEAPON/" + base;
    // The match script resolves loot/weapon_ids.csv through this base variant
    // even when there are no blueprints or attachments.
    Json tables = {{tableBase + "_progression.csv", Json::array({Json::array({"0", "", "", "", "", ""})})},
                   {tableBase + "_variants.csv", Json::array({variant})}};
    if (!project.is_null() && project.contains("tables"))
        tables = project.at("tables");
    if (!tables.is_object() || tables.size() > 16)
        throw std::runtime_error("invalid weapon table collection");
    for (const auto &[tableName, rows] : tables.items())
    {
        if ((!tableName.starts_with(tableBase + "_") && !tableName.starts_with("loot/" + base + "_")) ||
            !tableName.ends_with(".csv") || tableName.find("..") != std::string::npos ||
            !rows.is_array() || rows.empty() || rows.size() > 4096 || !rows[0].is_array() ||
            rows[0].empty() || rows[0].size() > 256)
            throw std::runtime_error("invalid project string table");
        const size_t columns = rows[0].size(), rowCount = rows.size();
        std::vector<std::string> values;
        for (const auto &row : rows)
        {
            if (!row.is_array() || row.size() != columns)
                throw std::runtime_error("ragged project string table");
            for (const auto &value : row)
                values.push_back(text(value));
        }
        writer.add(static_cast<IW8_XAssetType>(54), tableName, [tableName, values, columns, rowCount](ZoneWriter &w) {
            // Replay Load_StringTable (E05520): 48-byte header, then name, ushort
            // cells, uint hashes, and XString dictionary in the virtual stream.
            std::vector<std::string> strings;
            std::vector<uint16_t> cells;
            std::vector<uint32_t> hashes;
            for (const auto &value : values)
            {
                auto found = std::find(strings.begin(), strings.end(), value);
                cells.push_back(static_cast<uint16_t>(found - strings.begin()));
                if (found == strings.end())
                {
                    strings.push_back(value);
                    uint32_t hash = 0;
                    for (unsigned char c : value)
                        hash = hash * 31 + (c >= 'A' && c <= 'Z' ? c + 32 : c);
                    hashes.push_back(hash);
                }
            }
            std::vector<uint8_t> header(48);
            put(header, 0, PTR_FOLLOWS);
            put(header, 8, uint32_t(columns));
            put(header, 12, uint32_t(rowCount));
            put(header, 16, uint32_t(strings.size()));
            for (size_t offset : {size_t(24), size_t(32), size_t(40)})
                put(header, offset, PTR_FOLLOWS);
            w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
            w.align(7);
            w.write(header.data(), header.size());
            w.pushStream(XFILE_BLOCK_VIRTUAL);
            w.writeStr(tableName);
            w.align(1);
            w.write(cells.data(), cells.size() * sizeof(uint16_t));
            w.align(3);
            w.write(hashes.data(), hashes.size() * sizeof(uint32_t));
            w.align(7);
            for (size_t i = 0; i < strings.size(); ++i)
                w.writeT(PTR_FOLLOWS);
            for (const auto &value : strings)
                w.writeStr(value);
            w.popStream();
            w.popStream();
        });
    }
    writer.build();
    zt::Iw8WriteParams params{};
    for (unsigned i = 0; i < 11; ++i)
        params.blockSize[i] = writer.blockSize(i);
    params.totalDecompressed = writer.totalDecompressed();
    params.calcSize = writer.calcSize();
    const fs::path output = required("-o");
    fs::create_directories(output);
    if (!zt::iw8_write((output / (base + ".ff")).string(), writer.body(), params))
        return 1;
    // Match stock ownership: global_stream_mp owns the weapon, common_mp owns
    // its full SFX package and referenced banks. A global bank reference can
    // retain a name-only bank after common_mp unloads, invalidating SND sorting.
    const auto common = base + "_common";
    ZoneWriter audio;
    std::set<std::pair<unsigned, std::string>> audioReferences;
    for (const auto &key : forwardAssets)
        if (key.first != 42)
            audioReferences.insert(key);
    Replacements audioOwned;
    audioOwned[{78, sfxName}] = sfxName;
    if (!sounds.empty())
        audioOwned[{22, customBankName}] = customBankName;
    // A cloned WeaponAnimPackage can point directly at one of the imported
    // XAnimParts below. Mark those names as owned before walking package
    // relocations so they resolve to the native asset emitted in this common
    // zone instead of an external comma dependency with the same name.
    for (const auto &animation : animations)
        audioOwned[{7, text(animation.at("asset"))}] = text(animation.at("asset"));
    for (const auto &asset : owned)
        if (asset.at("pool") != 42)
            audioOwned[{asset.at("pool").get<unsigned>(), text(asset.at("name"))}] = text(asset.at("name"));
    prepareRecord(audio, sfx, audioReferences, audioOwned);
    for (auto &asset : owned)
        if (asset.at("pool") != 42)
            prepareRecord(audio, asset["root"], audioReferences, audioOwned);
    for (const auto &[type, dep] : audioReferences)
        dependency(audio, type, dep);
    for (const auto &animation : animations)
        addAnimation(audio, text(animation.at("asset")), animation);
    if (!sounds.empty())
        addResidentSoundAssets(audio, customBankName, std::move(customBankBytes), sounds, base);
    audio.add(static_cast<IW8_XAssetType>(78), sfxName,
              [sfx, owner = AssetKey{78, sfxName}](ZoneWriter &w) {
                  emitRecord(w, sfx, true, &owner);
              });
    for (const auto &asset : owned)
        if (asset.at("pool") != 42)
        {
            const AssetKey owner{asset.at("pool").get<unsigned>(), text(asset.at("name"))};
            audio.add(static_cast<IW8_XAssetType>(owner.first), owner.second,
                      [data = asset.at("root"), owner](ZoneWriter &w) {
                          emitRecord(w, data, true, &owner);
                      });
        }
    audio.build();
    zt::Iw8WriteParams audioParams{};
    for (unsigned i = 0; i < 11; ++i)
        audioParams.blockSize[i] = audio.blockSize(i);
    audioParams.totalDecompressed = audio.totalDecompressed();
    audioParams.calcSize = audio.calcSize();
    if (!zt::iw8_write((output / (common + ".ff")).string(), audio.body(), audioParams))
        return 1;
    // Replay's normal global-zone request expands into shader, worldwide and
    // language companions. Emit real native zones, including valid-empty ones.
    for (const auto &zone : {"techsets_" + base, "ww_" + base, "eng_" + base, "techsets_" + common,
                             "ww_" + common, "eng_" + common})
    {
        ZoneWriter companion;
        if (zone == "techsets_" + base && !material.material.empty())
            replayrender::RegisterMaterial(companion, material);
        if (zone == "eng_" + base)
        {
            std::map<std::string, std::string> localized{
                     {"custom_weapon/" + base, display},
                     {"custom_weapon_desc/" + base, project.is_null() ? "Custom base pistol. No attachments."
                                                                   : text(project.value("description", Json("Custom weapon.")))}};
            if (!project.is_null())
                for (const auto &attachment : project.value("attachment_rows", Json::array()))
                    localized["custom_attachment/" + text(attachment.at("asset"))] = text(attachment.at("display_name"));
            for (const auto &[key, value] : localized)
            {
                companion.add(static_cast<IW8_XAssetType>(41), key, [key, value](ZoneWriter &w) {
                    w.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
                    w.align(7);
                    w.writeT(std::array<uint64_t, 2>{PTR_FOLLOWS, PTR_FOLLOWS});
                    w.pushStream(XFILE_BLOCK_VIRTUAL);
                    w.writeStr(key);
                    w.writeStr(value);
                    w.popStream();
                    w.popStream();
                });
            }
        }
        companion.build();
        zt::Iw8WriteParams companionParams{};
        for (unsigned i = 0; i < 11; ++i)
            companionParams.blockSize[i] = companion.blockSize(i);
        companionParams.totalDecompressed = companion.totalDecompressed();
        companionParams.calcSize = companion.calcSize();
        if (!zt::iw8_write((output / (zone + ".ff")).string(), companion.body(), companionParams))
            return 1;
    }
    Json metadata{{"format", project.is_null() ? "replay-custom-weapon-v2" : "replay-custom-weapon-v3"},
                        {"base", base},
                        {"asset", name},
                        {"display_name", display},
                        {"reference", project.is_null() ? reference.at("source_weapon")
                                                       : project.value("loadout_reference", reference.at("source_weapon"))},
                        {"attachments", attachments},
                        {"loadout_slot", slot}};
    if (!project.is_null())
        for (const char *key : {"attachment_map", "attachment_rows", "default_attachments"})
            if (project.contains(key))
                metadata[key] = project.at(key);
    std::ofstream manifest(output / (base + ".weapon.json"));
    manifest << metadata.dump(2) << '\n';
    if (!manifest)
        throw std::runtime_error("could not write weapon registration metadata");
    zt::info("standalone weapon '%s': %zu base assets plus native techsets/ww/eng companions",
             name.c_str(), writer.assetCount());
    return 0;
}

} // namespace iw8
