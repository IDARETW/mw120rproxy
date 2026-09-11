#include "common/ff_io.h"
#include "common/fs_util.h"
#include "common/json.hpp"
#include "common/log.h"
#include "convert/maps_convert.h"
#include "dumpsrc/dump_source.h"
#include "dumpsrc/image_dump.h"
#include "dumpsrc/material_dumpsrc.h"
#include "dumpsrc/xmodel_dump.h"
#include "iw3/iw3_fastfile.h"
#include "iw7/iw7_fastfile.h"
#include "iw8/iw8_ffheader.h"
#include "iw8/iw8_zone.h"
#include "iw8/map_zone.h"
#include "iw8/maps_write.h"
#include "iw8/replay_havok.h"
#include "iw8/replay_impact.h"
#include "iw8/replay_render.h"
#include "iw8/write_xsurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace zt;

namespace
{
struct Args
{
    std::string command;
    std::vector<std::string> positional;
    std::string outputDirectory;
    std::string replayExecutable;
    std::string collisionPath;
    std::string footstepsPath;
    std::string unlinkerExecutable;
    std::vector<std::string> searchPaths;
    std::string metadataPath;
    std::string lightingProfile = "source";
    float sunIntensityScale = 1.0f;
};

struct MapMetadata
{
    bool present{};
    std::string id;
    std::string title;
    std::string description;
};

void printUsage()
{
    std::printf("iw8-zonetool - IW8 Replay 1.20 map compiler\n"
                "usage:\n"
                "  iw8-zonetool build-map <dump> <map> [-o <output>] [options]\n"
                "  iw8-zonetool build-iw3 <map.ff> [map] [-o <output>] [options]\n"
                "  iw8-zonetool inspect-iw7 <file.ff>\n"
                "  iw8-zonetool inspect <file.ff>\n"
                "  iw8-zonetool validate-output <map_output> <map>\n"
                "options:\n"
                "  --replay <game_dx12_ship_replay.exe>\n"
                "  --collision <collision.bin>\n"
                "  --footsteps <footsteps.bin>\n"
                "  --unlinker <OpenAssetTools\\Unlinker.exe>\n"
                "  --search-path <IW3 asset directory>\n"
                "  --metadata <map.json>\n"
                "  --lighting-profile source|aniyah-incursion\n"
                "  --sun-intensity-scale <positive multiplier>\n"
                "  -v  -q\n");
}

bool parseArgs(const int argc, char **argv, Args &args)
{
    if (argc < 2)
    {
        return false;
    }

    args.command = argv[1];
    for (int index = 2; index < argc; ++index)
    {
        const std::string value = argv[index];
        if (value == "-o" && index + 1 < argc)
        {
            args.outputDirectory = argv[++index];
        }
        else if (value == "--replay" && index + 1 < argc)
        {
            args.replayExecutable = argv[++index];
        }
        else if (value == "--collision" && index + 1 < argc)
        {
            args.collisionPath = argv[++index];
        }
        else if (value == "--footsteps" && index + 1 < argc)
        {
            args.footstepsPath = argv[++index];
        }
        else if (value == "--unlinker" && index + 1 < argc)
        {
            args.unlinkerExecutable = argv[++index];
        }
        else if (value == "--search-path" && index + 1 < argc)
        {
            args.searchPaths.emplace_back(argv[++index]);
        }
        else if (value == "--metadata" && index + 1 < argc)
        {
            args.metadataPath = argv[++index];
        }
        else if (value == "--lighting-profile" && index + 1 < argc)
        {
            args.lightingProfile = argv[++index];
        }
        else if (value == "--sun-intensity-scale" && index + 1 < argc)
        {
            const std::string scale = argv[++index];
            size_t consumed = 0;
            try
            {
                args.sunIntensityScale = std::stof(scale, &consumed);
            }
            catch (const std::exception &)
            {
                return false;
            }
            if (consumed != scale.size() || !std::isfinite(args.sunIntensityScale) ||
                args.sunIntensityScale <= 0.0f)
            {
                return false;
            }
        }
        else if (value == "-v")
        {
            g_logLevel = 3;
        }
        else if (value == "-q")
        {
            g_logLevel = 0;
        }
        else
        {
            args.positional.push_back(value);
        }
    }
    return true;
}

bool isMapId(const std::string &map)
{
    if (map.size() < 4 || map.size() > 15 || !map.starts_with("mp_"))
    {
        return false;
    }
    return std::all_of(map.begin(), map.end(), [](const unsigned char character) {
        return character == '_' || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9');
    });
}

bool requireMapId(const std::string &map)
{
    if (isMapId(map))
    {
        return true;
    }
    err("map id '%s' is invalid; use at most 15 lower-case characters in mp_<name>",
        map.c_str());
    return false;
}

std::array<std::string, 5> zoneNames(const std::string &map)
{
    return {map, "srv_" + map, "eng_" + map, "ww_" + map, "techsets_" + map};
}

std::vector<std::string> expectedFiles(const std::string &map, const bool includeMetadata = false)
{
    std::vector<std::string> files;
    for (const std::string &zone : zoneNames(map))
    {
        files.push_back(zone + ".ff");
    }
    if (includeMetadata)
    {
        files.emplace_back("map.json");
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool readMetadata(const std::string &path, MapMetadata &metadata)
{
    metadata = {};
    if (path.empty())
    {
        return true;
    }

    std::ifstream input(path);
    if (!input)
    {
        err("metadata: cannot read %s", path.c_str());
        return false;
    }

    try
    {
        const nlohmann::json data = nlohmann::json::parse(input);
        if (!data.is_object())
        {
            throw std::runtime_error("the root must be an object");
        }
        const auto readString = [&data](const char *key, std::string &value) {
            const auto item = data.find(key);
            if (item == data.end())
            {
                return true;
            }
            if (!item->is_string())
            {
                return false;
            }
            value = item->get<std::string>();
            return true;
        };
        if (!readString("id", metadata.id) || !readString("title", metadata.title) ||
            !readString("description", metadata.description))
        {
            throw std::runtime_error("id, title, and description must be strings");
        }
        metadata.present = true;
    }
    catch (const std::exception &exception)
    {
        err("metadata: %s is invalid: %s", path.c_str(), exception.what());
        return false;
    }
    return true;
}

bool validateMetadata(const std::string &directory, const std::string &map, bool &present)
{
    const std::filesystem::path path = std::filesystem::path(directory) / "map.json";
    present = std::filesystem::exists(path);
    if (!present)
    {
        return true;
    }

    MapMetadata metadata;
    if (!readMetadata(path.string(), metadata))
    {
        return false;
    }
    if (!metadata.id.empty() && metadata.id != map)
    {
        err("metadata: id '%s' does not match output map '%s'", metadata.id.c_str(), map.c_str());
        return false;
    }
    return true;
}

bool writeMetadata(const std::string &directory, const MapMetadata &metadata)
{
    if (!metadata.present)
    {
        return true;
    }

    nlohmann::json data = nlohmann::json::object();
    if (!metadata.id.empty())
    {
        data["id"] = metadata.id;
    }
    if (!metadata.title.empty())
    {
        data["title"] = metadata.title;
    }
    if (!metadata.description.empty())
    {
        data["description"] = metadata.description;
    }
    std::ofstream output(std::filesystem::path(directory) / "map.json", std::ios::binary);
    if (!output)
    {
        err("metadata: cannot write map.json");
        return false;
    }
    output << data.dump(2) << '\n';
    return output.good();
}

bool prepareOutputDirectory(const std::string &outputDirectory, const std::string &map,
                            const bool includeMetadata)
{
    std::error_code error;
    if (!std::filesystem::exists(outputDirectory, error))
    {
        return mkdirs(outputDirectory);
    }
    if (error || !std::filesystem::is_directory(outputDirectory, error))
    {
        err("build-map: output is not a directory: %s", outputDirectory.c_str());
        return false;
    }

    const std::vector<std::string> expected = expectedFiles(map, includeMetadata);
    for (const auto &entry : std::filesystem::directory_iterator(outputDirectory, error))
    {
        if (error || !entry.is_regular_file(error) ||
            std::find(expected.begin(), expected.end(), entry.path().filename().string()) ==
                expected.end())
        {
            err("build-map: output directory must be empty or contain only this map's files");
            return false;
        }
    }
    return !error;
}

bool validateZoneFile(const std::string &path)
{
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes) || bytes.size() < 0x8C)
    {
        err("validate: cannot read a complete IW8 header from %s", path.c_str());
        return false;
    }

    IW8_DB_FFHeader header{};
    std::memcpy(&header, bytes.data(), (std::min)(bytes.size(), sizeof(header)));
    if (std::memcmp(header.magic, iw8ff::kMagicUnsec, 8) != 0 ||
        header.headerVersion != iw8ff::kHeaderVersion ||
        header.xfileVersion != iw8ff::kXFileVersion || header.dashCompressBuild != 0 ||
        header.dashEncryptBuild != 0)
    {
        err("validate: %s has an unsupported Replay header", path.c_str());
        return false;
    }
    if (std::memcmp(bytes.data() + 0x88, "\x01IWC", 4) != 0 ||
        header.xfileHeader.size != bytes.size() - 0x8C ||
        header.residentPartSize != bytes.size() - 0x88 ||
        header.alwaysLoadedPartSize != header.xfileHeader.size)
    {
        err("validate: %s has invalid resident framing", path.c_str());
        return false;
    }

    uint64_t streamTotal = 0;
    for (const uint64_t size : header.xfileHeader.blockSize)
    {
        streamTotal += size;
    }
    if (streamTotal < header.xfileHeader.size)
    {
        err("validate: %s has invalid stream sizes", path.c_str());
        return false;
    }
    return true;
}

bool validatePackage(const std::string &packageDirectory, const std::string &map)
{
    if (!requireMapId(map))
    {
        return false;
    }

    bool hasMetadata = false;
    if (!validateMetadata(packageDirectory, map, hasMetadata))
    {
        return false;
    }

    std::vector<std::string> actual;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(packageDirectory, error))
    {
        if (error || !entry.is_regular_file(error))
        {
            err("validate: package contains an unreadable or non-file entry");
            return false;
        }
        actual.push_back(entry.path().filename().string());
    }
    if (error)
    {
        err("validate: cannot enumerate %s", packageDirectory.c_str());
        return false;
    }
    std::sort(actual.begin(), actual.end());
    if (actual != expectedFiles(map, hasMetadata))
    {
        err("validate: output must contain exactly five map fastfiles and optional map.json");
        return false;
    }

    bool valid = true;
    for (const std::string &zone : zoneNames(map))
    {
        valid = validateZoneFile(path_join(packageDirectory, zone + ".ff")) && valid;
    }
    if (valid)
    {
        info("validate: map output '%s' passes structural checks", map.c_str());
    }
    return valid;
}

const char *defaultEntities = "{ 212 \"worldspawn\" }\n"
                              "{ 212 \"info_player_start\" 709 \"0 0 64\" 80 \"0 0 0\" }\n"
                              "{ 212 \"mp_tdm_spawn\" 709 \"0 0 64\" 80 \"0 0 0\" }\n"
                              "{ 212 \"mp_tdm_spawn_allies_start\" 709 \"-64 0 64\" 80 \"0 0 0\" }\n"
                              "{ 212 \"mp_tdm_spawn_axis_start\" 709 \"64 0 64\" 80 \"0 180 0\" }\n"
                              "{ 212 \"script_model\" 709 \"0 0 16\" 80 \"0 0 0\" }\n";

struct AssetTally
{
    int materials{};
    int images{};
    int models{};
    int surfaces{};
    int failures{};
};

AssetTally addMapAssets(iw8::ZoneWriter &writer, const std::string &dumpDirectory,
                        const dumpsrc::DumpSource &source, const std::string &map)
{
    AssetTally tally;
    for (const std::string &relativePath : source.listMaterials())
    {
        const std::string path = path_join(path_join(dumpDirectory, "materials"), relativePath);
        if (convdump::mtl::emitMaterialFromDump(writer, path, relativePath))
        {
            ++tally.materials;
        }
        else
        {
            ++tally.failures;
        }
    }

    const std::string compassName = "compass_map_" + map;
    bool hasCompass = false;
    for (const std::string &name : dumpimg::listImageDumps(dumpDirectory))
    {
        const dumpimg::ImageDumpFile input = dumpimg::readImageDump(dumpDirectory, name);
        if (!input.loaded || !dumpimg::addImageAsset(writer, dumpimg::convertImage(input)))
        {
            ++tally.failures;
            continue;
        }
        ++tally.images;
        hasCompass = hasCompass || input.name == compassName;
    }
    if (hasCompass)
    {
        if (!convdump::mtl::writeCompassMaterial(writer, compassName))
        {
            ++tally.failures;
        }
    }
    else
    {
        warn("assets: no images/%s.dds, .iwi, or .ffImg; HUD compass will use the default material",
             compassName.c_str());
    }

    for (const std::string &name : source.listXModels())
    {
        if (iw8::addXModelFromDump(writer, dumpDirectory, name))
        {
            ++tally.models;
        }
        else
        {
            ++tally.failures;
        }
    }

    std::vector<std::string> surfaceFiles;
    if (list_dir(path_join(dumpDirectory, "XSurface"), surfaceFiles))
    {
        for (const std::string &file : surfaceFiles)
        {
            if (!file.ends_with(".xse"))
            {
                continue;
            }
            const std::string name = file.substr(0, file.size() - 4);
            if (iw8xs_dump::writeXModelSurfsFromDump(writer, dumpDirectory, name))
            {
                ++tally.surfaces;
            }
            else
            {
                ++tally.failures;
            }
        }
    }

    info("assets: %d materials, %d images, %d models, %d model surfaces, %d "
         "skipped",
         tally.materials, tally.images, tally.models, tally.surfaces, tally.failures);
    return tally;
}

template <typename Buffer> Iw8WriteParams writeParams(const Buffer &buffer)
{
    Iw8WriteParams params;
    for (int index = 0; index < iw8::IW8_MAX_XFILE_COUNT; ++index)
    {
        params.blockSize[index] = buffer.blockSize(index);
        params.totalDecompressed += params.blockSize[index];
    }
    params.calcSize = buffer.calcSize();
    return params;
}

template <> Iw8WriteParams writeParams(const iw8::ZoneBuffer &buffer)
{
    Iw8WriteParams params;
    for (int index = 0; index < iw8::IW8_MAX_XFILE_COUNT; ++index)
    {
        params.blockSize[index] = buffer.streamSize(index);
        params.totalDecompressed += params.blockSize[index];
    }
    params.calcSize = buffer.calcSize();
    return params;
}

iw8::MapSun loadLighting(const Args &args, const std::string &dumpDirectory,
                         const std::string &assetName)
{
    if (args.lightingProfile != "source" && args.lightingProfile != "aniyah-incursion")
    {
        throw std::runtime_error("unknown lighting profile: " + args.lightingProfile);
    }

    iw8::MapSun lighting;
    if (args.lightingProfile == "source")
    {
        const std::string path = path_join(dumpDirectory, assetName + ".lighting.json");
        if (!file_exists(path))
        {
            throw std::runtime_error("source lighting profile needs .lighting.json");
        }

        std::ifstream input(path);
        const nlohmann::json data = nlohmann::json::parse(input);
        if (data.at("schema") != 1)
        {
            throw std::runtime_error("invalid map lighting schema");
        }
        lighting.intensity = data.at("intensity").get<float>();

        float directionLength = 0.0f;
        float upLength = 0.0f;
        float dot = 0.0f;
        for (size_t index = 0; index < 3; ++index)
        {
            lighting.color[index] = data.at("color").at(index).get<float>();
            lighting.direction[index] = data.at("direction").at(index).get<float>();
            lighting.up[index] = data.at("up").at(index).get<float>();
            if (!std::isfinite(lighting.color[index]) || lighting.color[index] < 0.0f)
            {
                throw std::runtime_error("invalid sun color");
            }
            directionLength += lighting.direction[index] * lighting.direction[index];
            upLength += lighting.up[index] * lighting.up[index];
            dot += lighting.direction[index] * lighting.up[index];
        }
        if (!std::isfinite(lighting.intensity) || lighting.intensity < 0.0f ||
            !std::isfinite(directionLength + upLength + dot) ||
            std::abs(directionLength - 1.0f) > 0.001f ||
            (upLength != 0.0f && std::abs(upLength - 1.0f) > 0.001f) || std::abs(dot) > 0.001f)
        {
            throw std::runtime_error("invalid sun lighting data");
        }

        if (data.contains("primary_lights"))
        {
            const auto &sourceLights = data.at("primary_lights");
            if (!sourceLights.is_array() || sourceLights.size() < 2 ||
                sourceLights.size() > 65535)
            {
                throw std::runtime_error("invalid source primary-light array");
            }
            lighting.sunPrimaryLightIndex = data.at("sun_primary_light_index").get<uint32_t>();
            if (lighting.sunPrimaryLightIndex >= sourceLights.size())
            {
                throw std::runtime_error("invalid source sun primary-light index");
            }

            const auto readVector = [](const nlohmann::json &value, const char *name) {
                if (!value.is_array() || value.size() != 3)
                    throw std::runtime_error(std::string("invalid primary-light ") + name);
                std::array<float, 3> result{};
                for (size_t component = 0; component < result.size(); ++component)
                {
                    result[component] = value.at(component).get<float>();
                    if (!std::isfinite(result[component]))
                        throw std::runtime_error(std::string("invalid primary-light ") + name);
                }
                return result;
            };

            lighting.primaryLights.reserve(sourceLights.size());
            for (const auto &source : sourceLights)
            {
                iw8::MapSun::PrimaryLight light;
                const unsigned type = source.at("type").get<unsigned>();
                const unsigned exponent = source.at("exponent").get<unsigned>();
                if (type > 3 || exponent > 255)
                    throw std::runtime_error("invalid IW3 primary-light type or exponent");
                light.type = static_cast<uint8_t>(type);
                light.exponent = static_cast<uint8_t>(exponent);

                const auto color = readVector(source.at("color"), "color");
                const auto direction = readVector(source.at("direction"), "direction");
                const auto origin = readVector(source.at("origin"), "origin");
                if (std::ranges::any_of(color, [](const float value) { return value < 0.0f; }))
                    throw std::runtime_error("invalid primary-light color");
                std::copy(color.begin(), color.end(), light.color);
                std::copy(origin.begin(), origin.end(), light.origin);

                const float sourceDirectionLength = std::sqrt(
                    direction[0] * direction[0] + direction[1] * direction[1] +
                    direction[2] * direction[2]);
                if (type != 0 && sourceDirectionLength <= 0.000001f)
                    throw std::runtime_error("primary-light direction has zero length");
                if (sourceDirectionLength > 0.000001f)
                    for (size_t component = 0; component < direction.size(); ++component)
                        light.direction[component] = direction[component] / sourceDirectionLength;

                if (type >= 2)
                {
                    const std::array<float, 3> reference =
                        std::abs(light.direction[2]) < 0.999f
                            ? std::array<float, 3>{0.0f, 0.0f, 1.0f}
                            : std::array<float, 3>{0.0f, 1.0f, 0.0f};
                    const float projection = reference[0] * light.direction[0] +
                                             reference[1] * light.direction[1] +
                                             reference[2] * light.direction[2];
                    float sourceUpLength = 0.0f;
                    for (size_t component = 0; component < reference.size(); ++component)
                    {
                        light.up[component] =
                            reference[component] - projection * light.direction[component];
                        sourceUpLength += light.up[component] * light.up[component];
                    }
                    sourceUpLength = std::sqrt(sourceUpLength);
                    for (float &component : light.up)
                        component /= sourceUpLength;
                }

                light.radius = source.at("radius").get<float>();
                light.cosHalfFovOuter = source.at("cos_half_fov_outer").get<float>();
                light.cosHalfFovInner = source.at("cos_half_fov_inner").get<float>();
                light.rotationLimit = source.at("rotation_limit").get<float>();
                light.translationLimit = source.at("translation_limit").get<float>();
                light.definition = source.at("definition").get<std::string>();
                if (!std::isfinite(light.radius) || light.radius < 0.0f ||
                    !std::isfinite(light.cosHalfFovOuter) ||
                    !std::isfinite(light.cosHalfFovInner) ||
                    !std::isfinite(light.rotationLimit) ||
                    !std::isfinite(light.translationLimit) || light.definition.size() > 255)
                {
                    throw std::runtime_error("invalid primary-light scalar data");
                }
                if (type == 2 &&
                    (light.cosHalfFovOuter <= 0.0f ||
                     light.cosHalfFovOuter >= light.cosHalfFovInner ||
                     light.cosHalfFovInner > 1.0f))
                {
                    throw std::runtime_error("invalid IW3 spot-light field of view");
                }
                lighting.primaryLights.push_back(std::move(light));
            }
        }
    }

    if (lighting.primaryLights.empty())
        lighting.primaryLights.resize(2);

    lighting.intensity *= args.sunIntensityScale;
    if (!std::isfinite(lighting.intensity))
    {
        throw std::runtime_error("scaled sun intensity is not finite");
    }
    info("lighting: %s, scale %.6f, native intensity %.6f", args.lightingProfile.c_str(),
         args.sunIntensityScale, lighting.intensity);
    return lighting;
}

iw8::havok::BakeResult loadCollision(const Args &args, const std::string &dumpDirectory,
                                     const std::string &assetName)
{
    if (!args.collisionPath.empty())
    {
        if (args.replayExecutable.empty())
        {
            throw std::runtime_error("--replay is required with --collision");
        }
        return iw8::havok::BakeCollision(
            {args.replayExecutable, args.collisionPath, args.footstepsPath});
    }

    iw8::havok::BakeResult collision;
    const std::string path = path_join(dumpDirectory, assetName + ".havok");
    if (!read_file(path, collision.world) || collision.world.size() < 16 ||
        collision.world.size() > 256 * 1024 * 1024 ||
        std::memcmp(collision.world.data() + 4, "TAG0", 4) != 0)
    {
        throw std::runtime_error("native collision is missing; pass --replay and --collision");
    }
    return collision;
}

int writeMapPackage(const Args &args, const std::string &map, const std::string &outputDirectory,
                    const std::string &entities, const iw8::MapBounds &bounds,
                    const std::string &dumpDirectory, const MapMetadata &metadata)
{
    if (!prepareOutputDirectory(outputDirectory, map, metadata.present))
    {
        return 2;
    }

    const std::string assetName = "maps/mp/" + map + ".d3dbsp";
    const iw8::MapSun lighting = loadLighting(args, dumpDirectory, assetName);
    int result = 0;

    {
        iw8::ZoneBuffer buffer;
        auto collision = loadCollision(args, dumpDirectory, assetName);
        if (collision.models.empty())
        {
            iw8::havok::CollisionModel worldModel;
            if (bounds.valid)
            {
                std::copy_n(bounds.mn, 3, worldModel.minimum.begin());
                std::copy_n(bounds.mx, 3, worldModel.maximum.begin());
            }
            collision.models.push_back(worldModel);
        }
        info("collision: serialized native world %zu bytes, entities %zu bytes, %zu brush models",
             collision.world.size(), collision.entities.size(), collision.models.size());
        iw8::buildSrvMapZone(buffer, assetName.c_str(), entities, bounds, lighting, collision);
        if (!iw8_write(path_join(outputDirectory, "srv_" + map + ".ff"), buffer.data(),
                       writeParams(buffer)))
        {
            result = 1;
        }
    }

    {
        iw8::ZoneWriter writer;
        const std::string renderPath = path_join(dumpDirectory, assetName + ".render.json");
        if (!file_exists(renderPath))
        {
            throw std::runtime_error("native render data is missing: " + renderPath);
        }

        replayrender::RegisterMaterial(writer, renderPath);
        iw8::impact::Register(writer, map);
        const auto primaryLightCount = static_cast<uint32_t>(lighting.primaryLights.size());
        const auto sunPrimaryLightIndex = lighting.sunPrimaryLightIndex;
        writer.add(ASSET_TYPE_GFX_MAP, assetName,
                   [assetName, renderPath, primaryLightCount,
                    sunPrimaryLightIndex](iw8::ZoneWriter &output) {
                       iw8maps::emitGfxMapBody(output, assetName.c_str(), renderPath,
                                               primaryLightCount, sunPrimaryLightIndex);
                   });
        writer.add(ASSET_TYPE_GLASS_MAP, assetName, [assetName](iw8::ZoneWriter &output) {
            iw8maps::emitGlassMapBody(output, assetName.c_str());
        });
        const dumpsrc::DumpSource source(dumpDirectory, map);
        addMapAssets(writer, dumpDirectory, source, map);
        writer.build();

        if (!iw8_write(path_join(outputDirectory, map + ".ff"), writer.body(), writeParams(writer)))
        {
            result = 1;
        }
        info("build-map: main zone contains %zu assets", writer.assetCount());
    }

    const auto writeEmpty = [&](const std::string &name) {
        iw8::ZoneBuffer buffer;
        iw8::buildEmptyZone(buffer);
        if (!iw8_write(path_join(outputDirectory, name), buffer.data(), writeParams(buffer)))
        {
            result = 1;
        }
    };
    writeEmpty("eng_" + map + ".ff");
    writeEmpty("ww_" + map + ".ff");
    writeEmpty("techsets_" + map + ".ff");

    if (result == 0 && !writeMetadata(outputDirectory, metadata))
    {
        result = 1;
    }

    if (result == 0 && !validatePackage(outputDirectory, map))
    {
        result = 1;
    }
    if (result == 0)
    {
        info("build-map: wrote map output to %s", outputDirectory.c_str());
    }
    return result;
}

int buildMap(const Args &args)
{
    if (args.positional.size() < 2)
    {
        printUsage();
        return 2;
    }

    const std::string dumpDirectory = args.positional[0];
    const std::string map = args.positional[1];
    if (!requireMapId(map))
    {
        return 2;
    }

    MapMetadata metadata;
    if (!readMetadata(args.metadataPath, metadata))
    {
        return 2;
    }
    if (!metadata.id.empty() && metadata.id != map)
    {
        err("metadata: id '%s' does not match map '%s'", metadata.id.c_str(), map.c_str());
        return 2;
    }
    const std::string outputDirectory = args.outputDirectory.empty()
                                            ? path_join(dumpDirectory, map + "_out")
                                            : args.outputDirectory;

    const dumpsrc::DumpSource source(dumpDirectory, map);
    if (!source.hasMapFiles())
    {
        err("build-map: no maps/mp/%s.d3dbsp.* files under %s", map.c_str(), dumpDirectory.c_str());
        return 1;
    }

    std::string entities;
    bool foundEntities = false;
    for (const std::string &path : {path_join(dumpDirectory, map + "_iw8_ents.txt"),
                                    path_join(path_dir(dumpDirectory), map + "_iw8_ents.txt")})
    {
        if (file_exists(path) && read_file_str(path, entities) && !entities.empty())
        {
            info("entities: using %s", path.c_str());
            foundEntities = true;
            break;
        }
    }

    const dumpsrc::EntsDump sourceEntities = source.loadEnts();
    if (!foundEntities && sourceEntities.loaded && !sourceEntities.text.empty())
    {
        const size_t converted = convert::iw3ToIw8EntityString(sourceEntities.text, entities);
        if (converted == 0)
        {
            err("build-map: could not convert any source entities");
            return 1;
        }
        info("entities: converted %zu source entities", converted);
        foundEntities = true;
    }
    if (!foundEntities)
    {
        entities = defaultEntities;
        warn("entities: using the built-in Replay TDM spawn set");
    }
    convert::validateIw8EntityString(entities);

    iw8::MapBounds bounds;
    const dumpsrc::ClipMapDump clipMap = source.loadClipMap();
    if (clipMap.loaded && clipMap.boundsFromVerts)
    {
        for (size_t index = 0; index < 3; ++index)
        {
            bounds.mn[index] = clipMap.boundsMin[index];
            bounds.mx[index] = clipMap.boundsMax[index];
        }
        bounds.valid = true;
    }

    const std::string boundsPath =
        path_join(dumpDirectory, "maps/mp/" + map + ".d3dbsp.bounds.json");
    if (file_exists(boundsPath))
    {
        std::ifstream file(boundsPath);
        const nlohmann::json data = nlohmann::json::parse(file);
        if (data.at("schema") != 1 || data.at("min").size() != 3 || data.at("max").size() != 3)
        {
            throw std::runtime_error("invalid map bounds schema");
        }
        for (size_t index = 0; index < 3; ++index)
        {
            bounds.mn[index] = data.at("min").at(index).get<float>();
            bounds.mx[index] = data.at("max").at(index).get<float>();
            if (!std::isfinite(bounds.mn[index]) || !std::isfinite(bounds.mx[index]) ||
                bounds.mn[index] >= bounds.mx[index] || std::abs(bounds.mn[index]) > 100100.0f ||
                std::abs(bounds.mx[index]) > 100100.0f)
            {
                throw std::runtime_error("invalid map bounds");
            }
        }
        bounds.valid = true;
    }

    const dumpsrc::ComWorldDump commonWorld = source.loadComWorld();
    if (commonWorld.loaded)
    {
        info("common world: %d primary lights", commonWorld.primaryLightCount);
    }

    return writeMapPackage(args, map, outputDirectory, entities, bounds, dumpDirectory, metadata);
}

int buildIw3(const Args &args)
{
    if (args.positional.empty() || args.positional.size() > 2 || args.replayExecutable.empty())
    {
        printUsage();
        return 2;
    }

    const bool hasExplicitMap = args.positional.size() == 2;
    std::string map = hasExplicitMap ? args.positional[1]
                                     : std::filesystem::path(args.positional[0]).stem().string();
    MapMetadata metadata;
    if (!readMetadata(args.metadataPath, metadata))
    {
        return 2;
    }
    if (!metadata.id.empty())
    {
        if (hasExplicitMap && metadata.id != map)
        {
            err("metadata: id '%s' does not match map '%s'", metadata.id.c_str(), map.c_str());
            return 2;
        }
        map = metadata.id;
    }
    if (!requireMapId(map))
    {
        return 2;
    }

    iw8::havok::PrepareCollisionBaker(args.replayExecutable);

    iw3::ImportOptions options;
    options.fastfile = args.positional[0];
    options.map = map;
    options.unlinker = args.unlinkerExecutable;
    for (const std::string &path : args.searchPaths)
    {
        options.searchPaths.emplace_back(path);
    }
    iw3::PreparedMap prepared = iw3::PrepareFastfile(options);

    Args build = args;
    build.command = "build-map";
    build.positional = {prepared.root.string(), map};
    build.collisionPath = prepared.collision.string();
    if (build.outputDirectory.empty())
    {
        build.outputDirectory =
            (std::filesystem::path(args.positional[0]).parent_path() / (map + "_iw8")).string();
    }
    return buildMap(build);
}

int inspect(const Args &args)
{
    if (args.positional.empty())
    {
        printUsage();
        return 2;
    }
    return inspect_ff(args.positional[0]) ? 0 : 1;
}

int inspectIw7(const Args &args)
{
    if (args.positional.empty())
    {
        printUsage();
        return 2;
    }

    iw7::FastfileInfo fastfile;
    std::string failure;
    if (!iw7::inspectFastfile(args.positional[0], fastfile, failure))
    {
        err("inspect-iw7: %s", failure.c_str());
        return 1;
    }

    info("inspect-iw7: version %u, %s", fastfile.version,
         fastfile.signedFile ? "signed" : "unsigned");
    info("inspect-iw7: %u script strings, %u assets, %llu decompressed bytes",
         fastfile.scriptStringCount, fastfile.assetCount,
         static_cast<unsigned long long>(fastfile.uncompressedSize));
    info("inspect-iw7: %u shared streams, %u image streams", fastfile.sharedStreamCount,
         fastfile.imageStreamCount);

    const std::vector<std::string> packages = iw7::requiredPackages(fastfile);
    if (packages.empty())
    {
        info("inspect-iw7: no external image packages");
    }
    else
    {
        std::string list;
        for (const std::string &package : packages)
        {
            if (!list.empty())
            {
                list += ", ";
            }
            list += package;
        }
        info("inspect-iw7: external packages: %s", list.c_str());
    }
    return 0;
}

int validate(const Args &args)
{
    if (args.positional.size() < 2)
    {
        printUsage();
        return 2;
    }
    return validatePackage(args.positional[0], args.positional[1]) ? 0 : 1;
}
} // namespace

int main(const int argc, char **argv)
try
{
    Args args;
    if (!parseArgs(argc, argv, args))
    {
        printUsage();
        return 2;
    }
    if (args.command == "build-map")
    {
        return buildMap(args);
    }
    if (args.command == "build-iw3")
    {
        return buildIw3(args);
    }
    if (args.command == "inspect")
    {
        return inspect(args);
    }
    if (args.command == "inspect-iw7")
    {
        return inspectIw7(args);
    }
    if (args.command == "validate-output" || args.command == "validate-package")
    {
        return validate(args);
    }

    err("unknown command '%s'", args.command.c_str());
    printUsage();
    return 2;
}
catch (const std::exception &error)
{
    err("conversion failed: %s", error.what());
    return 1;
}
