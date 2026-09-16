#include "iw3_fastfile.h"
#include "iw3_lightgrid.h"
#include "iw3_render_assets.h"

#include "../convert/maps_convert.h"
#include "../convert/xsurface_convert.h"
#include "../dumpsrc/xse_dump.h"

#include "common/fs_util.h"
#include "common/json.hpp"
#include "common/log.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace iw3
{
namespace
{
using Json = nlohmann::json;
using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;

struct Vertex
{
    Vec3 position{};
    Vec3 normal{};
    Vec3 tangent{};
    Vec2 uv{};
    Vec2 lightmapUv{};
    float binormalSign{1.0f};
    bool authoredTangent{};
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
};

struct Surface
{
    std::string material;
    int lightmap{-1};
    std::uint16_t reflectionProbe{};
    std::uint32_t visibilityGroup{UINT32_MAX};
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct BrushModel
{
    Vec3 minimum{};
    Vec3 maximum{};
    std::vector<Surface> surfaces;
};

struct SourceStaticModelLod
{
    float distance{};
    std::vector<Surface> surfaces;
};

struct SourceStaticModel
{
    std::string name;
    std::size_t collisionLod{SIZE_MAX};
    std::vector<SourceStaticModelLod> lods;
};

struct SourceStaticModelInstance
{
    std::size_t model{};
    Vec3 origin{};
    std::array<Vec3, 3> axis{};
    float scale{1.0f};
};

struct SourceStaticModels
{
    std::vector<SourceStaticModel> models;
    std::vector<SourceStaticModelInstance> instances;
};

struct SourceDynamicEntities
{
    struct Brush
    {
        Vec4 quaternion{};
        Vec3 origin{};
        std::uint16_t model{};
        std::uint16_t physicsModel{};
    };

    SourceStaticModels models;
    std::vector<std::size_t> definitionModels;
    std::vector<Vec4> quaternions;
    std::vector<Vec3> origins;
    std::vector<Brush> brushes;
};

struct CollisionHull
{
    std::vector<Vec3> points;
    struct Slab
    {
        Vec3 direction{};
        float midpoint{};
        float halfSize{};
    };
    std::vector<Slab> slabs;
    std::uint32_t contents{1};
    std::uint32_t model{};
    std::uint32_t surfaceFlags{};
    std::uint16_t glassId{};
    std::vector<Vec4> ladderPlanes;
};

struct CollisionModel
{
    Vec3 minimum{};
    Vec3 maximum{};
};

struct CollisionMesh
{
    struct Triangle
    {
        std::array<std::uint32_t, 3> indices{};
        std::uint32_t contents{1};
        std::uint32_t surfaceFlags{};
        std::uint32_t material{5};
    };

    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
    std::uint32_t model{};
};

struct CollisionData
{
    std::vector<CollisionHull> hulls;
    std::vector<CollisionModel> models;
    std::vector<CollisionMesh> meshes;
};

struct VisibilityGroups
{
    static constexpr std::uint32_t Global = UINT32_MAX;
    static constexpr std::uint32_t Unassigned = UINT32_MAX - 1;

    std::vector<std::uint32_t> surfaces;
    std::vector<std::uint32_t> models;
    std::vector<Json> treeBounds;
    std::vector<std::vector<std::uint32_t>> cellTrees;
    std::size_t sourceTreeCount{};
};

float Dot(const Vec3 &a, const Vec3 &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 Add(const Vec3 &a, const Vec3 &b)
{
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Vec3 Subtract(const Vec3 &a, const Vec3 &b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 Multiply(const Vec3 &a, const float scale)
{
    return {a[0] * scale, a[1] * scale, a[2] * scale};
}

Vec3 Cross(const Vec3 &a, const Vec3 &b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

Vec3 Unit(const Vec3 &value)
{
    const float length = std::sqrt(Dot(value, value));
    if (!std::isfinite(length) || length < 1.0e-10f)
    {
        throw std::runtime_error("degenerate vector in IW3 map data");
    }
    return Multiply(value, 1.0f / length);
}

template <std::size_t Size> std::array<float, Size> ReadVector(const Json &value)
{
    if (!value.is_array() || value.size() != Size)
    {
        throw std::runtime_error("invalid vector in IW3 map export");
    }
    std::array<float, Size> result{};
    for (std::size_t index = 0; index < Size; ++index)
    {
        result[index] = value.at(index).get<float>();
        if (!std::isfinite(result[index]))
        {
            throw std::runtime_error("non-finite value in IW3 map export");
        }
    }
    return result;
}

std::wstring Quote(const std::wstring &value)
{
    if (value.find_first_of(L" \t\"") == std::wstring::npos)
    {
        return value;
    }

    std::wstring output = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\')
        {
            ++slashes;
            continue;
        }
        if (character == L'\"')
        {
            output.append(slashes * 2 + 1, L'\\');
            output.push_back(character);
            slashes = 0;
            continue;
        }
        output.append(slashes, L'\\');
        slashes = 0;
        output.push_back(character);
    }
    output.append(slashes * 2, L'\\');
    output.push_back(L'\"');
    return output;
}

std::filesystem::path FindUnlinker(const std::filesystem::path &requested)
{
    std::vector<std::filesystem::path> candidates;
    if (!requested.empty())
    {
        candidates.push_back(requested);
    }

    wchar_t environment[32768]{};
    DWORD environmentLength =
        GetEnvironmentVariableW(L"IW8_ZONETOOL_UNLINKER", environment, std::size(environment));
    if (!environmentLength)
    {
        environmentLength =
            GetEnvironmentVariableW(L"MW120R_UNLINKER", environment, std::size(environment));
    }
    if (environmentLength && environmentLength < std::size(environment))
    {
        candidates.emplace_back(environment);
    }

    wchar_t module[MAX_PATH]{};
    const DWORD moduleLength = GetModuleFileNameW(nullptr, module, std::size(module));
    if (moduleLength && moduleLength < std::size(module))
    {
        auto directory = std::filesystem::path(module).parent_path();
        for (unsigned depth = 0; depth < 6 && !directory.empty(); ++depth)
        {
            candidates.push_back(directory / "Unlinker.exe");
            candidates.push_back(directory / "tools" / "Unlinker.exe");
            candidates.push_back(directory / "mw120rproxy" / "tools" / "_vendor" /
                                 "OpenAssetTools" / "build" / "bin" / "Release_x86" /
                                 "Unlinker.exe");
            directory = directory.parent_path();
        }
    }
    auto directory = std::filesystem::current_path();
    for (unsigned depth = 0; depth < 4 && !directory.empty(); ++depth)
    {
        candidates.push_back(directory / "Unlinker.exe");
        candidates.push_back(directory / "tools" / "Unlinker.exe");
        candidates.push_back(directory / "mw120rproxy" / "tools" / "_vendor" / "OpenAssetTools" /
                             "build" / "bin" / "Release_x86" / "Unlinker.exe");
        directory = directory.parent_path();
    }

    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"Unlinker.exe", nullptr, std::size(found), found, nullptr))
    {
        candidates.emplace_back(found);
    }

    std::error_code error;
    for (const auto &candidate : candidates)
    {
        if (std::filesystem::is_regular_file(candidate, error))
        {
            return std::filesystem::absolute(candidate);
        }
        error.clear();
    }
    throw std::runtime_error(
        "OpenAssetTools Unlinker was not found; pass --unlinker or set IW8_ZONETOOL_UNLINKER");
}

std::filesystem::path MakeScratchDirectory(const std::filesystem::path &scratchRoot)
{
    std::error_code error;
    std::filesystem::create_directories(scratchRoot, error);
    if (error)
    {
        throw std::runtime_error("cannot create the IW3 conversion scratch parent: " +
                                 scratchRoot.string());
    }
    const auto root = std::filesystem::absolute(scratchRoot).wstring() + L"\\";

    wchar_t temporaryFile[MAX_PATH]{};
    if (!GetTempFileNameW(root.c_str(), L"iw3", 0, temporaryFile) || !DeleteFileW(temporaryFile) ||
        !CreateDirectoryW(temporaryFile, nullptr))
    {
        throw std::runtime_error("cannot create a private IW3 conversion directory");
    }
    return std::filesystem::path(temporaryFile);
}

void RunUnlinker(const std::filesystem::path &unlinker, const ImportOptions &options,
                 const std::filesystem::path &output,
                 const std::filesystem::path &sourceFastfile = {},
                 const bool supplementalAssets = false)
{
    std::vector<std::wstring> arguments{unlinker.wstring(),
                                        L"--no-color",
                                        L"--image-format",
                                        L"DDS",
                                        L"--model-format",
                                        L"OBJ",
                                        L"-o",
                                        output.wstring()};
    if (supplementalAssets)
    {
        arguments.push_back(L"--include-assets");
        arguments.push_back(L"xmodel,material,image,fx");
    }

    std::vector<std::filesystem::path> searchPaths;
    const auto appendSearchPath = [&searchPaths](const std::filesystem::path &path) {
        const auto directory = path.filename().empty() ? path.parent_path() : path;
        searchPaths.push_back(directory);
        auto main = directory / "main";
        if (directory.filename() == "zone")
            main = directory.parent_path() / "main";
        else if (directory.filename() == "english" &&
                 directory.parent_path().filename() == "zone")
            main = directory.parent_path().parent_path() / "main";
        std::error_code error;
        if (std::filesystem::is_regular_file(main / "iw_00.iwd", error))
            searchPaths.push_back(std::move(main));
    };
    for (const auto &path : options.searchPaths)
        appendSearchPath(path);
    appendSearchPath(options.fastfile.parent_path());
    if (!searchPaths.empty())
    {
        std::wstring joined;
        for (const auto &path : searchPaths)
        {
            if (!joined.empty())
            {
                joined.push_back(L';');
            }
            joined += std::filesystem::absolute(path).wstring();
        }
        arguments.push_back(L"--search-path");
        arguments.push_back(std::move(joined));
    }

    const auto inputFastfile = sourceFastfile.empty() ? options.fastfile : sourceFastfile;
    arguments.push_back(std::filesystem::absolute(inputFastfile).wstring());
    const auto loadFastfile =
        inputFastfile.parent_path() / (inputFastfile.stem().wstring() + L"_load.ff");
    std::error_code error;
    if (!supplementalAssets && std::filesystem::is_regular_file(loadFastfile, error))
    {
        arguments.push_back(std::filesystem::absolute(loadFastfile).wstring());
    }

    std::wstring commandLine;
    for (const auto &argument : arguments)
    {
        if (!commandLine.empty())
        {
            commandLine.push_back(L' ');
        }
        commandLine += Quote(argument);
    }

    const auto logPath =
        output / (supplementalAssets ? "unlinker-supplemental.log" : "unlinker.log");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error("cannot create the temporary Unlinker log");
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL started =
        CreateProcessW(unlinker.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(log);
    if (!started)
    {
        throw std::runtime_error("cannot start OpenAssetTools Unlinker");
    }

    const DWORD timeoutMinutes = supplementalAssets ? 30u : 10u;
    const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMinutes * 60u * 1000u);
    DWORD exitCode = 1;
    if (wait == WAIT_TIMEOUT)
    {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    else if (wait == WAIT_OBJECT_0)
    {
        GetExitCodeProcess(process.hProcess, &exitCode);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (wait != WAIT_OBJECT_0 || exitCode != 0)
    {
        std::ifstream logFile(logPath, std::ios::binary);
        std::string logTail;
        if (logFile)
        {
            logFile.seekg(0, std::ios::end);
            const auto size = logFile.tellg();
            if (size > 0)
            {
                const auto length = static_cast<std::streamoff>(std::min<std::streamoff>(size, 4096));
                logFile.seekg(size - length);
                logTail.resize(static_cast<std::size_t>(length));
                logFile.read(logTail.data(), length);
            }
        }
        throw std::runtime_error("OpenAssetTools failed while reading the IW3 fastfile" +
                                 (logTail.empty() ? std::string{} : ":\n" + logTail));
    }
}

std::filesystem::path FindIw3CommonFastfile(const ImportOptions &options)
{
    std::vector<std::filesystem::path> roots = options.searchPaths;
    roots.push_back(options.fastfile.parent_path());
    std::vector<std::filesystem::path> candidates;
    for (const auto &root : roots)
    {
        candidates.push_back(root);
        candidates.push_back(root / "common_mp.ff");
        candidates.push_back(root / "zone" / "english" / "common_mp.ff");
        candidates.push_back(root / "zone" / "common_mp.ff");
        candidates.push_back(root.parent_path() / "zone" / "english" / "common_mp.ff");
        candidates.push_back(root.parent_path() / "zone" / "common_mp.ff");
    }

    std::error_code error;
    for (const auto &candidate : candidates)
    {
        if (candidate.filename() == "common_mp.ff" &&
            std::filesystem::is_regular_file(candidate, error))
        {
            return std::filesystem::absolute(candidate);
        }
        error.clear();
    }
    return {};
}

std::vector<std::string> MissingEntityModels(const std::filesystem::path &root,
                                             const std::vector<std::string> &models)
{
    std::vector<std::string> missing;
    for (const auto &model : models)
    {
        std::error_code error;
        const auto path = root / "xmodel" / (model + ".json");
        if (!std::filesystem::is_regular_file(path, error))
            missing.push_back(model);
    }
    return missing;
}

std::string JoinNames(const std::vector<std::string> &names)
{
    std::string result;
    for (const auto &name : names)
    {
        if (!result.empty())
            result += ", ";
        result += name;
    }
    return result;
}

std::filesystem::path FindSingleExport(const std::filesystem::path &root,
                                       const std::string &extension)
{
    std::error_code error;
    std::filesystem::path result;
    for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file(error))
        {
            continue;
        }
        const std::string name = iterator->path().filename().string();
        if (!name.ends_with(extension))
        {
            continue;
        }
        if (!result.empty())
        {
            throw std::runtime_error("IW3 input contains more than one map world");
        }
        result = iterator->path();
    }
    if (error || result.empty())
    {
        throw std::runtime_error(
            "Unlinker did not produce Replay map data; build it with ReplayMapDumpers.h");
    }
    return result;
}

Json ReadJson(const std::filesystem::path &path);
std::string TrimSourceAssetField(std::string_view value);

std::vector<PreparedFx> ReadPreparedFx(const std::filesystem::path &root)
{
    const auto validName = [](const std::string &name) {
        if (name.empty() || name.size() > 256 || name.find('\0') != std::string::npos)
            return false;
        const std::filesystem::path path(name);
        if (path.has_root_path())
            return false;
        for (const auto &part : path)
            if (part == "." || part == "..")
                return false;
        return true;
    };
    const auto fxRoot = root / "fx";
    std::vector<PreparedFx> effects;
    std::error_code error;
    if (!std::filesystem::is_directory(fxRoot, error))
    {
        return effects;
    }

    for (std::filesystem::recursive_directory_iterator iterator(fxRoot, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file(error) || !iterator->path().filename().string().ends_with(".iw3.json"))
            continue;

        const Json graph = ReadJson(iterator->path());
        if (graph.value("schema", 0) != 1 || graph.value("asset_type", std::string{}) != "iw3_fx")
            throw std::runtime_error("invalid IW3 FX source export: " + iterator->path().string());
        const auto name = graph.value("name", std::string{});
        if (!validName(name))
            throw std::runtime_error("IW3 FX source export has an invalid name: " +
                                     iterator->path().string());

        PreparedFx prepared;
        prepared.name = name;
        prepared.sourcePath = iterator->path();
        const auto dependencies = graph.value("dependencies", Json::array());
        if (!dependencies.is_array() || dependencies.size() > 4096)
            throw std::runtime_error("IW3 FX source export has an invalid dependency list: " +
                                     iterator->path().string());
        for (const auto &dependency : dependencies)
        {
            if (!dependency.is_object())
                throw std::runtime_error("IW3 FX source export has an invalid dependency: " +
                                         iterator->path().string());
            const auto dependencyName = dependency.value("name", std::string{});
            const auto dependencyType = dependency.value("type", std::string{});
            if (!validName(dependencyName) || dependencyType.empty() ||
                (dependencyType != "fx" && dependencyType != "material" &&
                 dependencyType != "image" && dependencyType != "xmodel"))
                throw std::runtime_error("IW3 FX source export has an unsupported dependency: " +
                                         iterator->path().string());
            prepared.dependencies.push_back({dependencyName, dependencyType});
        }
        effects.push_back(std::move(prepared));
    }
    if (error)
        throw std::runtime_error("cannot enumerate IW3 FX source exports");
    std::sort(effects.begin(), effects.end(), [](const PreparedFx &left, const PreparedFx &right) {
        return left.name < right.name;
    });
    for (std::size_t index = 1; index < effects.size(); ++index)
    {
        if (effects[index - 1].name == effects[index].name)
            throw std::runtime_error("duplicate IW3 FX source export: " + effects[index].name);
    }
    return effects;
}

std::set<std::string> ReadDeclaredFx(const std::filesystem::path &zoneFile)
{
    std::ifstream input(zoneFile);
    if (!input)
        throw std::runtime_error("cannot read IW3 FX zone declarations: " + zoneFile.string());
    std::set<std::string> names;
    std::string line;
    while (std::getline(input, line))
    {
        const auto comma = line.find(',');
        if (comma == std::string::npos ||
            TrimSourceAssetField(std::string_view(line).substr(0, comma)) != "fx")
            continue;
        const auto name = TrimSourceAssetField(
            std::string_view(line).substr(line.rfind(',') + 1));
        if (name.empty())
            continue;
        const std::filesystem::path asset(name);
        if (asset.has_root_path() ||
            std::find(asset.begin(), asset.end(), std::filesystem::path("..")) != asset.end())
            throw std::runtime_error("invalid IW3 FX declaration name: " + name);
        names.insert(name);
    }
    if (input.bad())
        throw std::runtime_error("cannot read IW3 FX zone declarations");
    return names;
}

std::vector<std::string> MissingDeclaredFx(const std::filesystem::path &root)
{
    std::set<std::string> missing;
    std::error_code error;
    const auto zoneRoot = root / "zone_source";
    if (!std::filesystem::is_directory(zoneRoot, error))
        return {};
    for (std::filesystem::directory_iterator iterator(zoneRoot, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file(error) || iterator->path().extension() != ".zone")
            continue;
        for (const auto &name : ReadDeclaredFx(iterator->path()))
            if (!std::filesystem::is_regular_file(root / "fx" / (name + ".iw3.json"), error))
                missing.insert(name);
    }
    if (error)
        throw std::runtime_error("cannot enumerate IW3 FX zone declarations");
    return {missing.begin(), missing.end()};
}

bool IsKnownIw3SourceAssetType(const std::string_view type)
{
    // These names are the canonical IW3 names emitted by OpenAssetTools'
    // ZoneDefWriterIW3. Keep this list closed: an unrecognised declaration
    // must not disappear silently when a new Unlinker build adds an asset.
    constexpr std::array<std::string_view, 33> knownTypes{
        "xmodelpieces", "physpreset",       "xanim",      "xmodel",   "material",
        "techniqueset", "image",            "sound",      "soundcurve", "loadedsound",
        "clipmap_unused", "clipmap",         "comworld",   "gameworldsp", "gameworldmp",
        "mapents",      "gfxworld",         "lightdef",   "uimap",      "font",
        "menulist",     "menu",             "localize",   "weapon",     "snddriverglobals",
        "fx",           "impactfx",         "aitype",     "mptype",     "character",
        "xmodelalias",  "rawfile",          "stringtable"};
    return std::find(knownTypes.begin(), knownTypes.end(), type) != knownTypes.end();
}

bool IsConsumedIw3SourceAssetType(const std::string_view type)
{
    // ReplayMapDumpers exports the roots and entity text, while build-iw3
    // converts the reachable model/material/image dependency graph. This is a
    // source-consumption classification, not a claim that every declaration of
    // a dependency type becomes a top-level Replay asset.
    constexpr std::array<std::string_view, 8> consumedTypes{
        "clipmap", "comworld", "gameworldmp", "mapents",
        "gfxworld", "image", "material", "xmodel"};
    return std::find(consumedTypes.begin(), consumedTypes.end(), type) != consumedTypes.end();
}

std::string_view SourceAssetAuditReason(const std::string_view type)
{
    if (type == "techniqueset")
        return "source techniquesets are not portable; reachable materials use generated Replay "
               "techniquesets";
    if (type == "fx")
        return "no IW8-native source-FX graph emitter is built by build-iw3";
    if (type == "impactfx")
        return "the source impact graph is not converted; the package contains the fixed native "
               "Replay impact table";
    if (type == "sound" || type == "soundcurve" || type == "loadedsound")
        return "audio declarations are outside the native map package";
    if (type == "physpreset")
        return "native collision is built from the clipmap export rather than IW3 presets";
    if (type == "lightdef")
        return "only map-local light_point_linear has a matched Replay LightDef conversion";
    if (type == "gameworldsp")
        return "build-iw3 accepts multiplayer map roots only";
    if (type == "rawfile")
        return "raw files are not imported generically; only explicitly consumed map data is read";
    return "the declaration has no native build-iw3 consumer";
}

std::string TrimSourceAssetField(const std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::string SourceAssetExamples(const std::set<std::string> &names)
{
    std::string result;
    std::size_t count = 0;
    for (const auto &name : names)
    {
        if (!result.empty())
            result += ", ";
        result += name;
        if (++count == 3)
            break;
    }
    if (names.size() > count)
        result += ", ...";
    return result;
}

void AuditSourceAssetDeclarations(const std::filesystem::path &root)
{
    const auto zoneRoot = root / "zone_source";
    std::error_code error;
    if (!std::filesystem::is_directory(zoneRoot, error))
    {
        throw std::runtime_error(
            "OpenAssetTools did not produce IW3 zone declarations; source assets cannot be "
            "accounted for");
    }

    std::vector<std::filesystem::path> zoneFiles;
    for (std::filesystem::directory_iterator iterator(zoneRoot, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file(error) || iterator->path().extension() != ".zone")
            continue;
        zoneFiles.push_back(iterator->path());
    }
    if (error)
        throw std::runtime_error("cannot enumerate OpenAssetTools IW3 zone declarations");
    if (zoneFiles.empty())
    {
        throw std::runtime_error(
            "OpenAssetTools produced an empty IW3 zone_source directory; source assets cannot "
            "be accounted for");
    }
    std::sort(zoneFiles.begin(), zoneFiles.end());

    std::map<std::string, std::set<std::string>> declarations;
    for (const auto &zoneFile : zoneFiles)
    {
        std::ifstream input(zoneFile, std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot read OpenAssetTools IW3 zone declaration");

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (lineNumber == 1 && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line.erase(0, 3);
            }

            const auto firstComma = line.find(',');
            if (firstComma == std::string::npos)
                continue;
            std::string type = TrimSourceAssetField(std::string_view(line).substr(0, firstComma));
            if (type.empty() || type.front() == '#' || type.front() == '/' || type.front() == '>')
                continue;
            std::transform(type.begin(), type.end(), type.begin(), [](const char character) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            });
            if (!IsKnownIw3SourceAssetType(type))
            {
                throw std::runtime_error("OpenAssetTools emitted unknown IW3 source asset type '" +
                                         type + "' in zone declaration; update the closed "
                                               "build-iw3 asset classification");
            }

            const auto lastComma = line.rfind(',');
            std::string name = TrimSourceAssetField(
                std::string_view(line).substr(lastComma == std::string::npos ? firstComma + 1
                                                                              : lastComma + 1));
            if (name.empty())
                name = "<unnamed>";
            declarations[std::move(type)].insert(std::move(name));
        }
        if (input.bad())
            throw std::runtime_error("cannot read OpenAssetTools IW3 zone declaration");
    }

    std::size_t declarationCount = 0;
    std::string summary;
    for (const auto &[type, names] : declarations)
    {
        declarationCount += names.size();
        if (!summary.empty())
            summary += ", ";
        summary += type + "=" + std::to_string(names.size());
    }
    zt::info("iw3: accounted for %zu unique source asset declarations across %zu zone file(s): %s",
             declarationCount, zoneFiles.size(), summary.c_str());

    for (const auto &[type, names] : declarations)
    {
        if (IsConsumedIw3SourceAssetType(type))
            continue;
        const auto examples = SourceAssetExamples(names);
        zt::warn("iw3: source type '%s' has %zu declaration(s) outside the native map package "
                 "(%s); examples: %s",
                 type.c_str(), names.size(), SourceAssetAuditReason(type).data(),
                 examples.c_str());
    }
}

Json ReadJson(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("cannot read " + path.string());
    }
    return Json::parse(input);
}

void WriteJson(const std::filesystem::path &path, const Json &value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("cannot write " + path.string());
    }
    output << value;
    if (!output)
    {
        throw std::runtime_error("cannot finish " + path.string());
    }
}

Surface ReadWorldSurface(const Json &source)
{
    Surface output;
    output.material = source.at("material").get<std::string>();
    output.lightmap = source.value("lightmap", -1);
    // IW3 reserves the five-bit value 31 for an unlightmapped world surface.
    // Keep real indices range-checked later, but do not treat this sentinel as
    // a reference to a missing atlas entry.
    if (output.lightmap == 31)
        output.lightmap = -1;
    const auto reflectionProbe = source.at("reflection_probe").get<unsigned>();
    if (reflectionProbe > UINT16_MAX)
        throw std::runtime_error("IW3 surface reflection-probe index exceeds native width");
    output.reflectionProbe = static_cast<std::uint16_t>(reflectionProbe);
    const auto &vertices = source.at("vertices");
    const auto &indices = source.at("indices");
    if (!vertices.is_array() || vertices.empty() || !indices.is_array() || indices.size() % 3 != 0)
    {
        throw std::runtime_error("invalid IW3 world surface");
    }

    output.vertices.reserve(vertices.size());
    for (const auto &item : vertices)
    {
        Vertex vertex;
        vertex.position = ReadVector<3>(item.at("position"));
        vertex.normal = Unit(ReadVector<3>(item.at("normal")));
        vertex.tangent = ReadVector<3>(item.at("tangent"));
        if (Dot(vertex.tangent, vertex.tangent) < 1.0e-12f)
            vertex.tangent = Cross(
                std::abs(vertex.normal[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, vertex.normal);
        vertex.tangent = Unit(vertex.tangent);
        vertex.binormalSign = item.at("binormal_sign").get<float>();
        if (!std::isfinite(vertex.binormalSign) || std::abs(vertex.binormalSign) < 0.5f)
            throw std::runtime_error("invalid IW3 vertex tangent handedness");
        vertex.binormalSign = vertex.binormalSign < 0 ? -1.0f : 1.0f;
        vertex.authoredTangent = true;
        vertex.uv = ReadVector<2>(item.at("uv"));
        vertex.lightmapUv = ReadVector<2>(item.at("lightmap_uv"));
        if (item.contains("color"))
        {
            const auto &color = item.at("color");
            if (!color.is_array() || color.size() != 4)
            {
                throw std::runtime_error("invalid IW3 vertex color");
            }
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const int value = color.at(channel).get<int>();
                if (value < 0 || value > 255)
                {
                    throw std::runtime_error("invalid IW3 vertex color");
                }
                vertex.color[channel] = static_cast<std::uint8_t>(value);
            }
        }
        output.vertices.push_back(vertex);
    }
    output.indices.reserve(indices.size());
    for (const auto &item : indices)
    {
        const auto index = item.get<std::uint32_t>();
        if (index >= output.vertices.size())
        {
            throw std::runtime_error("IW3 surface index is outside its vertex array");
        }
        output.indices.push_back(index);
    }
    return output;
}

void AlignWinding(Surface &surface)
{
    for (std::size_t index = 0; index < surface.indices.size(); index += 3)
    {
        const Vertex &a = surface.vertices[surface.indices[index]];
        const Vertex &b = surface.vertices[surface.indices[index + 1]];
        const Vertex &c = surface.vertices[surface.indices[index + 2]];
        const Vec3 face = Cross(Subtract(b.position, a.position), Subtract(c.position, a.position));
        if (Dot(face, Add(Add(a.normal, b.normal), c.normal)) < 0.0f)
        {
            std::swap(surface.indices[index + 1], surface.indices[index + 2]);
        }
    }
}

std::filesystem::path SafeChild(const std::filesystem::path &root,
                                const std::filesystem::path &relative)
{
    if (relative.is_absolute())
    {
        throw std::runtime_error("absolute path in IW3 map export");
    }
    const auto canonicalRoot = std::filesystem::weakly_canonical(root);
    const auto candidate = std::filesystem::weakly_canonical(root / relative);
    const auto mismatch = std::mismatch(canonicalRoot.begin(), canonicalRoot.end(),
                                        candidate.begin(), candidate.end());
    if (mismatch.first != canonicalRoot.end())
    {
        throw std::runtime_error("path escapes the IW3 extraction directory");
    }
    return candidate;
}

VisibilityGroups ReadVisibilityGroups(const Json &world)
{
    const auto &surfaces = world.at("surfaces");
    const auto &models = world.value("models", Json::array());
    const auto &dpvs = world.at("dpvs");
    const auto &cells = dpvs.at("cells");
    const auto &planes = dpvs.at("planes");
    const auto &nodes = dpvs.at("nodes");
    if (!cells.is_array() || cells.empty() || cells.size() > 65535 || !planes.is_array() ||
        planes.size() > 65535 || !nodes.is_array() || nodes.empty() || nodes.size() > 65535)
        throw std::runtime_error("invalid IW3 DPVS topology");

    VisibilityGroups result;
    result.cellTrees.resize(cells.size());
    result.surfaces.assign(surfaces.size(), VisibilityGroups::Unassigned);
    result.models.assign(models.size(), VisibilityGroups::Unassigned);
    const auto assign = [](std::vector<std::uint32_t> &groups, const std::size_t index,
                           const std::uint32_t tree) {
        if (index >= groups.size())
            throw std::runtime_error("IW3 DPVS ownership index is outside its array");
        if (groups[index] == VisibilityGroups::Unassigned)
            groups[index] = tree;
        else if (groups[index] != tree)
            groups[index] = VisibilityGroups::Global;
    };
    for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
    {
        const auto &cell = cells.at(cellIndex);
        const auto &trees = cell.at("trees");
        if (!trees.is_array())
            throw std::runtime_error("invalid IW3 DPVS AABB trees");
        for (const auto &tree : trees)
        {
            ++result.sourceTreeCount;
            const auto &treeSurfaces = tree.at("surfaces");
            const auto &treeModels = tree.at("models");
            if (!treeSurfaces.is_array() || !treeModels.is_array())
                throw std::runtime_error("invalid IW3 DPVS AABB ownership");
            if (treeSurfaces.empty() && treeModels.empty())
                continue;
            if (result.treeBounds.size() >= VisibilityGroups::Unassigned)
                throw std::runtime_error("IW3 DPVS has too many AABB trees");
            const auto &bounds = tree.at("bounds");
            if (!bounds.is_array() || bounds.size() != 2)
                throw std::runtime_error("invalid IW3 DPVS AABB bounds");
            const Vec3 minimum = ReadVector<3>(bounds.at(0));
            const Vec3 maximum = ReadVector<3>(bounds.at(1));
            for (std::size_t axis = 0; axis < minimum.size(); ++axis)
                if (minimum[axis] > maximum[axis])
                    throw std::runtime_error("reversed IW3 DPVS AABB bounds");

            const auto group = static_cast<std::uint32_t>(result.treeBounds.size());
            result.treeBounds.push_back(bounds);
            result.cellTrees[cellIndex].push_back(group);
            for (const auto &surface : treeSurfaces)
                assign(result.surfaces, surface.get<std::size_t>(), group);
            for (const auto &model : treeModels)
                assign(result.models, model.get<std::size_t>(), group);
        }
    }
    for (auto &group : result.surfaces)
        if (group == VisibilityGroups::Unassigned)
            group = VisibilityGroups::Global;
    for (auto &group : result.models)
        if (group == VisibilityGroups::Unassigned)
            group = VisibilityGroups::Global;
    return result;
}

SourceStaticModel ReadSourceModel(const std::filesystem::path &root, const std::string &modelName,
                                  const bool placedStaticBindPose = false)
{
    const auto modelPath = SafeChild(root, std::filesystem::path("xmodel") / (modelName + ".json"));
    const Json model = ReadJson(modelPath);
    const auto modelType = model.value("type", std::string{});
    if ((modelType != "rigid" && !(placedStaticBindPose && modelType == "animated")) ||
        !model.contains("lods") ||
        model.at("lods").empty() || model.at("lods").size() > 6)
    {
        throw std::runtime_error("IW3 model has unsupported render LODs: " + modelName);
    }

    SourceStaticModel converted;
    converted.name = modelName;
    const int collisionLod = model.value("collLod", -1);
    if (collisionLod < -1 || collisionLod >= static_cast<int>(model.at("lods").size()))
        throw std::runtime_error("IW3 model has an invalid collision LOD: " + modelName);
    if (collisionLod >= 0)
        converted.collisionLod = static_cast<std::size_t>(collisionLod);
    const auto geometryPath =
        SafeChild(root, std::filesystem::path("xmodel") / (modelName + ".replay.json"));
    if (!std::filesystem::is_regular_file(geometryPath))
        throw std::runtime_error("IW3 model attributes are missing for '" + modelName +
                                 "'; configure and rebuild the bundled OpenAssetTools Unlinker");
    const Json geometry = ReadJson(geometryPath);
    if (geometry.value("schema", 0) != 1 || geometry.at("name") != modelName ||
        !geometry.at("lods").is_array() || geometry.at("lods").size() != model.at("lods").size())
        throw std::runtime_error("IW3 model geometry does not match its LOD table: " + modelName);
    float previousDistance = 0.0f;
    for (const auto &sourceLod : model.at("lods"))
    {
        SourceStaticModelLod lod;
        lod.distance = sourceLod.at("distance").get<float>();
        if (!std::isfinite(lod.distance) || lod.distance < previousDistance)
            throw std::runtime_error("IW3 model has invalid LOD distances: " + modelName);
        previousDistance = lod.distance;
        const auto &geometryLod = geometry.at("lods").at(converted.lods.size());
        if (geometryLod.at("distance").get<float>() != lod.distance ||
            !geometryLod.at("surfaces").is_array() || geometryLod.at("surfaces").empty())
            throw std::runtime_error("IW3 model has an empty render LOD: " + modelName);
        for (const auto &sourceSurface : geometryLod.at("surfaces"))
        {
            // OAT's IW3 model exporter writes verts0.xyz directly. Its GLTF bind
            // pose uses each baseMat and its inverse, so a placed static model's
            // deformed vertices have these exact model-space positions at rest.
            if (sourceSurface.at("deformed").get<bool>() && !placedStaticBindPose)
                throw std::runtime_error("IW3 placed model requires skinning: " + modelName);
            Surface surface = ReadWorldSurface(sourceSurface);
            // IW3 and Replay XSurfaces use the same clockwise winding. World-surface
            // normal alignment would turn these native model faces inside out.
            lod.surfaces.push_back(std::move(surface));
        }
        converted.lods.push_back(std::move(lod));
    }
    return converted;
}

// Replay's ladder IK advances its hand target by 12 units.  The IW3
// com_ladder_wood render mesh has authored rung bands every 24 units.  Keep
// the source model intact for its collision LOD, and add only the five
// missing render rung bands to each known authored LOD.
void AddLadderMidpointRungs(Surface &surface, const std::size_t lodIndex)
{
    constexpr std::array<float, 5> sourceRungCenters{24.0f, 48.0f, 72.0f, 96.0f, 120.0f};
    constexpr std::array<std::size_t, 2> expectedVertexCounts{232, 144};
    constexpr std::array<std::size_t, 2> expectedTriangleCounts{128, 64};
    constexpr float rungHalfExtent = 4.0f;

    if (lodIndex >= expectedVertexCounts.size() || surface.vertices.size() != expectedVertexCounts[lodIndex] ||
        surface.indices.size() != expectedTriangleCounts[lodIndex] * 3)
    {
        throw std::runtime_error("IW3 com_ladder_wood geometry does not match the midpoint-rung contract");
    }

    std::array<std::vector<std::array<std::uint32_t, 3>>, sourceRungCenters.size()> rungTriangles;
    for (std::size_t index = 0; index < surface.indices.size(); index += 3)
    {
        const std::array<std::uint32_t, 3> triangle{surface.indices[index], surface.indices[index + 1],
                                                    surface.indices[index + 2]};
        const auto &a = surface.vertices.at(triangle[0]);
        const auto &b = surface.vertices.at(triangle[1]);
        const auto &c = surface.vertices.at(triangle[2]);
        const float minimum = (std::min)({a.position[2], b.position[2], c.position[2]});
        const float maximum = (std::max)({a.position[2], b.position[2], c.position[2]});
        for (std::size_t rung = 0; rung < sourceRungCenters.size(); ++rung)
        {
            if (minimum >= sourceRungCenters[rung] - rungHalfExtent &&
                maximum <= sourceRungCenters[rung] + rungHalfExtent)
            {
                rungTriangles[rung].push_back(triangle);
                break;
            }
        }
    }

    const std::size_t expectedPerRung = lodIndex == 0 ? 12 : 8;
    for (const auto &triangles : rungTriangles)
        if (triangles.size() != expectedPerRung)
            throw std::runtime_error("IW3 com_ladder_wood rung bands are incomplete");

    for (std::size_t rung = 0; rung < rungTriangles.size(); ++rung)
        for (const auto &triangle : rungTriangles[rung])
        {
            const auto first = static_cast<std::uint32_t>(surface.vertices.size());
            for (const auto vertexIndex : triangle)
            {
                Vertex vertex = surface.vertices.at(vertexIndex);
                vertex.position[2] += 12.0f;
                surface.vertices.push_back(std::move(vertex));
            }
            surface.indices.insert(surface.indices.end(), {first, first + 1, first + 2});
        }
}

std::array<Vec3, 3> BasisFromQuaternion(const Vec4 &source)
{
    float lengthSquared = 0.0f;
    for (const float value : source)
    {
        if (!std::isfinite(value))
            throw std::runtime_error("IW3 dynamic entity has an invalid quaternion");
        lengthSquared += value * value;
    }
    if (lengthSquared < 0.99f || lengthSquared > 1.01f)
        throw std::runtime_error("IW3 dynamic entity has a non-unit quaternion");
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    const float x = source[0] * inverseLength;
    const float y = source[1] * inverseLength;
    const float z = source[2] * inverseLength;
    const float w = source[3] * inverseLength;
    return {{{1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w), 2.0f * (x * z - y * w)},
             {2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w)},
             {2.0f * (x * z + y * w), 2.0f * (y * z - x * w), 1.0f - 2.0f * (x * x + y * y)}}};
}

SourceDynamicEntities ReadDynamicEntities(const Json &collision, const std::filesystem::path &root)
{
    SourceDynamicEntities result;
    const auto modelCount = collision.value("dynamic_model_count", std::size_t{});
    const auto brushCount = collision.value("dynamic_brush_count", std::size_t{});
    if (!collision.contains("dynamic_models"))
    {
        if (modelCount)
            throw std::runtime_error(
                "IW3 dynamic definitions are missing; rebuild OpenAssetTools Unlinker");
        return result;
    }
    const auto &definitions = collision.at("dynamic_models");
    if (!definitions.is_array() || definitions.size() != modelCount)
        throw std::runtime_error("invalid IW3 dynamic-model definition table");

    std::unordered_map<std::string, std::size_t> modelCache;
    result.definitionModels.reserve(definitions.size());
    result.quaternions.reserve(definitions.size());
    result.origins.reserve(definitions.size());
    result.models.instances.reserve(definitions.size());
    for (const auto &definition : definitions)
    {
        if (definition.at("type").get<unsigned>() != 1)
            throw std::runtime_error("IW3 destructible dynamic models require Scriptable data");
        const std::string modelName = definition.at("model").get<std::string>();
        if (modelName.empty())
            throw std::runtime_error("IW3 dynamic model has no XModel");
        auto found = modelCache.find(modelName);
        if (found == modelCache.end())
        {
            const std::size_t modelIndex = result.models.models.size();
            result.models.models.push_back(ReadSourceModel(root, modelName));
            found = modelCache.emplace(modelName, modelIndex).first;
        }

        const Vec4 quaternion = ReadVector<4>(definition.at("quaternion"));
        const Vec3 origin = ReadVector<3>(definition.at("origin"));
        result.definitionModels.push_back(found->second);
        result.quaternions.push_back(quaternion);
        result.origins.push_back(origin);
        result.models.instances.push_back(
            {found->second, origin, BasisFromQuaternion(quaternion), 1.0f});
    }

    if (!collision.contains("dynamic_brushes"))
    {
        if (brushCount)
            throw std::runtime_error(
                "IW3 dynamic brush definitions are missing; rebuild OpenAssetTools Unlinker");
        return result;
    }
    const auto &brushes = collision.at("dynamic_brushes");
    if (!brushes.is_array() || brushes.size() != brushCount)
        throw std::runtime_error("invalid IW3 dynamic-brush definition table");
    const auto submodelCount = collision.at("submodel_count").get<std::size_t>();
    if (submodelCount > std::numeric_limits<std::uint16_t>::max())
        throw std::runtime_error("IW3 brush-model table exceeds Replay limits");
    result.brushes.reserve(brushes.size());
    for (const auto &definition : brushes)
    {
        if (definition.at("type").get<unsigned>() != 1)
            throw std::runtime_error("IW3 destructible dynamic brushes require Scriptable data");
        const auto brushModel = definition.at("brush_model").get<std::size_t>();
        const auto physicsModel = definition.at("physics_brush_model").get<std::size_t>();
        if (brushModel >= submodelCount || physicsModel >= submodelCount)
            throw std::runtime_error("IW3 dynamic brush references a missing brush model");
        if (brushModel != physicsModel)
        {
            throw std::runtime_error(
                "IW3 dynamic brush uses separate render and physics brush models");
        }
        SourceDynamicEntities::Brush converted;
        converted.quaternion = ReadVector<4>(definition.at("quaternion"));
        converted.origin = ReadVector<3>(definition.at("origin"));
        BasisFromQuaternion(converted.quaternion);
        converted.model = static_cast<std::uint16_t>(brushModel);
        converted.physicsModel = static_cast<std::uint16_t>(physicsModel);
        result.brushes.push_back(converted);
    }
    return result;
}

std::vector<BrushModel> ReadBrushModels(const Json &world, const std::filesystem::path &root,
                                        const VisibilityGroups &visibility,
                                        SourceStaticModels &staticModels)
{
    const auto &sourceSurfaces = world.at("surfaces");
    if (!sourceSurfaces.is_array() || sourceSurfaces.empty())
    {
        throw std::runtime_error("IW3 world contains no render surfaces");
    }

    std::vector<BrushModel> output;
    if (world.contains("brush_models"))
    {
        const auto &sourceModels = world.at("brush_models");
        if (!sourceModels.is_array() || sourceModels.empty() || sourceModels.size() > 65535)
        {
            throw std::runtime_error("invalid IW3 world brush-model table");
        }
        output.reserve(sourceModels.size());
        std::vector<bool> claimed(sourceSurfaces.size());
        for (const auto &sourceModel : sourceModels)
        {
            BrushModel model;
            const auto &bounds = sourceModel.at("bounds");
            if (!bounds.is_array() || bounds.size() != 2)
                throw std::runtime_error("invalid IW3 brush-model bounds");
            model.minimum = ReadVector<3>(bounds.at(0));
            model.maximum = ReadVector<3>(bounds.at(1));
            for (std::size_t axis = 0; axis < 3; ++axis)
                if (model.minimum[axis] > model.maximum[axis])
                    throw std::runtime_error("reversed IW3 brush-model bounds");

            const std::size_t count = sourceModel.at("count").get<std::size_t>();
            if (count)
            {
                const std::size_t first = sourceModel.at("start").get<std::size_t>();
                if (first > sourceSurfaces.size() || count > sourceSurfaces.size() - first)
                    throw std::runtime_error("invalid IW3 brush-model surface range");
                model.surfaces.reserve(count);
                for (std::size_t index = first; index < first + count; ++index)
                {
                    if (claimed[index])
                        throw std::runtime_error("overlapping IW3 brush-model surface ranges");
                    claimed[index] = true;
                    Surface surface = ReadWorldSurface(sourceSurfaces.at(index));
                    surface.visibilityGroup = visibility.surfaces.at(index);
                    AlignWinding(surface);
                    model.surfaces.push_back(std::move(surface));
                }
            }
            output.push_back(std::move(model));
        }
        if (std::find(claimed.begin(), claimed.end(), false) != claimed.end())
            throw std::runtime_error("IW3 brush-model table does not own every render surface");
    }
    else
    {
        const auto &bounds = world.at("bounds");
        if (!bounds.is_array() || bounds.size() != 2)
            throw std::runtime_error("invalid IW3 world bounds");
        BrushModel model;
        model.minimum = ReadVector<3>(bounds.at(0));
        model.maximum = ReadVector<3>(bounds.at(1));
        model.surfaces.reserve(sourceSurfaces.size());
        std::size_t sourceIndex = 0;
        for (const auto &sourceSurface : sourceSurfaces)
        {
            Surface surface = ReadWorldSurface(sourceSurface);
            surface.visibilityGroup = visibility.surfaces.at(sourceIndex++);
            AlignWinding(surface);
            model.surfaces.push_back(std::move(surface));
        }
        output.push_back(std::move(model));
    }

    std::unordered_map<std::string, std::size_t> modelCache;
    std::size_t instanceIndex = 0;
    for (const auto &instance : world.value("models", Json::array()))
    {
        std::string modelName = instance.at("model").get<std::string>();
        if (!modelName.empty() && modelName.front() == ',')
        {
            modelName.erase(0, 1);
            std::error_code error;
            const auto modelPath = SafeChild(root, std::filesystem::path("xmodel") /
                                                      (modelName + ".json"));
            if (modelName.empty() || !std::filesystem::is_regular_file(modelPath, error))
                throw std::runtime_error(
                    "IW3 static-model instance " + std::to_string(instanceIndex) +
                    " needs external xmodel '" + modelName +
                    "'; add the matching IW3 zone with --search-path");
        }

        auto found = modelCache.find(modelName);
        if (found == modelCache.end())
        {
            const std::size_t modelIndex = staticModels.models.size();
            staticModels.models.push_back(ReadSourceModel(root, modelName, true));
            found = modelCache.emplace(modelName, modelIndex).first;
        }

        SourceStaticModelInstance converted;
        converted.model = found->second;
        converted.origin = ReadVector<3>(instance.at("origin"));
        converted.scale = instance.at("scale").get<float>();
        if (!std::isfinite(converted.scale) || converted.scale <= 0.0f)
        {
            throw std::runtime_error("invalid IW3 static-model scale");
        }
        for (std::size_t row = 0; row < 3; ++row)
        {
            converted.axis[row] = ReadVector<3>(instance.at("axis").at(row));
        }
        staticModels.instances.push_back(std::move(converted));
        ++instanceIndex;
    }

    return output;
}

std::uint32_t PackedNormal(const Vec3 &source, const Vec3 &sourceTangent, const float binormalSign,
                           const bool authoredTangent)
{
    const Vec3 normal = Unit(source);
    Vec3 tangent = authoredTangent
                       ? sourceTangent
                       : Cross(std::abs(normal[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, normal);
    tangent = Subtract(tangent, Multiply(normal, Dot(tangent, normal)));
    if (Dot(tangent, tangent) < 1.0e-10f)
        tangent = Cross(std::abs(normal[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, normal);
    tangent = Unit(tangent);
    const Vec3 bitangent = Multiply(Cross(normal, tangent), binormalSign < 0 ? -1.0f : 1.0f);
    const float matrix[3][3] = {{tangent[0], bitangent[0], normal[0]},
                                {tangent[1], bitangent[1], normal[1]},
                                {tangent[2], bitangent[2], normal[2]}};
    std::array<float, 4> quaternion{};
    const float trace = matrix[0][0] + matrix[1][1] + matrix[2][2];
    if (trace > 0.0f)
    {
        const float scale = std::sqrt(trace + 1.0f) * 2.0f;
        quaternion = {(matrix[2][1] - matrix[1][2]) / scale, (matrix[0][2] - matrix[2][0]) / scale,
                      (matrix[1][0] - matrix[0][1]) / scale, scale / 4.0f};
    }
    else
    {
        std::size_t largest = 0;
        if (matrix[1][1] > matrix[largest][largest])
            largest = 1;
        if (matrix[2][2] > matrix[largest][largest])
            largest = 2;
        const std::size_t next = (largest + 1) % 3;
        const std::size_t last = (largest + 2) % 3;
        const float scale =
            std::sqrt(1.0f + matrix[largest][largest] - matrix[next][next] - matrix[last][last]) *
            2.0f;
        quaternion[largest] = scale / 4.0f;
        quaternion[next] = (matrix[next][largest] + matrix[largest][next]) / scale;
        quaternion[last] = (matrix[last][largest] + matrix[largest][last]) / scale;
        quaternion[3] = (matrix[last][next] - matrix[next][last]) / scale;
    }

    std::size_t largest = 0;
    for (std::size_t index = 1; index < quaternion.size(); ++index)
    {
        if (std::abs(quaternion[index]) >= std::abs(quaternion[largest]))
        {
            largest = index;
        }
    }
    const float scale = std::sqrt(2.0f) * (quaternion[largest] >= 0.0f ? 1.0f : -1.0f);
    std::array<std::uint32_t, 3> encoded{};
    std::size_t outputIndex = 0;
    for (std::size_t index = 0; index < quaternion.size(); ++index)
    {
        if (index == largest)
            continue;
        const unsigned bits = outputIndex < 2 ? 10u : 9u;
        const float maximum = static_cast<float>((1u << bits) - 1u);
        const int value = static_cast<int>((quaternion[index] * scale + 1.0f) * maximum / 2.0f);
        encoded[outputIndex++] =
            static_cast<std::uint32_t>(std::clamp(value, 0, static_cast<int>(maximum)));
    }
    std::uint32_t packed = encoded[0] | (encoded[1] << 10) | (encoded[2] << 20) |
                           (static_cast<std::uint32_t>(largest) << 30);
    if (binormalSign < 0)
        packed |= 1u << 29;
    return packed;
}

unsigned MaterialIndex(const RenderPlan &plan, const SurfaceKind kind)
{
    if (kind == SurfaceKind::opaque)
        return 0;
    const std::string suffix = kind == SurfaceKind::cutout  ? "_foliage"
                               : kind == SurfaceKind::glass ? "_glass"
                                                            : "_sky";
    for (std::size_t index = 0; index < plan.additionalMaterials.size(); ++index)
        if (plan.additionalMaterials[index].at("material").get<std::string>().ends_with(suffix))
            return static_cast<unsigned>(index + 1);
    throw std::runtime_error("IW3 render plan is missing a material variant");
}

Vec2 EncodedLightmap(const Vertex &vertex, const Surface &surface, const MaterialPlan &material,
                     const RenderPlan &plan)
{
    float x = 0, y = 0;
    if (surface.lightmap >= 0 && static_cast<std::size_t>(surface.lightmap) >= plan.lightmaps.size())
        throw std::runtime_error("IW3 surface references an invalid lightmap");
    const bool baked = surface.lightmap >= 0;
    if (baked)
    {
        const auto &rectangle = plan.lightmaps[surface.lightmap];
        x = (rectangle.x +
             std::clamp(vertex.lightmapUv[0] * rectangle.width, 0.5f, rectangle.width - 0.5f)) /
            4096.0f;
        y = (rectangle.y +
             std::clamp(vertex.lightmapUv[1] * rectangle.height, 0.5f, rectangle.height - 0.5f)) /
            4096.0f;
    }
    const unsigned kind = material.kind == SurfaceKind::cutout  ? 3u
                          : material.kind == SurfaceKind::glass ? 2u
                                                                : 0u;
    return {static_cast<float>(material.tile) + 0.25f + x * 0.25f,
            static_cast<float>(kind + material.flags + (baked ? 4u : 0u)) + 0.25f + y * 0.25f};
}

using EntityFields = std::unordered_map<std::string, std::string>;

std::vector<EntityFields> ParseEntityFields(const std::string &text)
{
    std::vector<EntityFields> entities;
    EntityFields entity;
    std::vector<std::string> values;
    std::string value;
    bool insideEntity = false;
    bool insideString = false;
    bool escaped = false;
    for (const char character : text)
    {
        if (insideString)
        {
            if (escaped)
            {
                value.push_back(character);
                escaped = false;
            }
            else if (character == '\\')
            {
                escaped = true;
            }
            else if (character == '"')
            {
                values.push_back(std::move(value));
                value.clear();
                insideString = false;
            }
            else
            {
                value.push_back(character);
            }
            continue;
        }
        if (character == '{')
        {
            if (insideEntity)
                throw std::runtime_error("nested IW3 entity block");
            insideEntity = true;
            entity.clear();
            values.clear();
        }
        else if (character == '"' && insideEntity)
        {
            insideString = true;
        }
        else if (character == '}' && insideEntity)
        {
            if (values.size() % 2)
                throw std::runtime_error("IW3 entity has an unpaired key or value");
            for (std::size_t index = 0; index < values.size(); index += 2)
                entity[values[index]] = values[index + 1];
            entities.push_back(entity);
            insideEntity = false;
        }
    }
    if (insideEntity || insideString)
        throw std::runtime_error("unterminated IW3 entity block");
    return entities;
}

Vec3 ParseVector(const std::string &value)
{
    std::istringstream input(value);
    Vec3 result{};
    if (!(input >> result[0] >> result[1] >> result[2]) ||
        std::any_of(result.begin(), result.end(),
                    [](const float item) { return !std::isfinite(item); }))
    {
        throw std::runtime_error("invalid IW3 entity vector");
    }
    return result;
}

std::array<Vec3, 3> EntityAxis(const EntityFields &entity)
{
    Vec3 angles{};
    if (const auto anglesValue = entity.find("angles"); anglesValue != entity.end())
        angles = ParseVector(anglesValue->second);
    else if (const auto angleValue = entity.find("angle"); angleValue != entity.end())
        angles[1] = std::stof(angleValue->second);

    constexpr float radians = 0.01745329251994329577f;
    const float pitch = angles[0] * radians;
    const float yaw = angles[1] * radians;
    const float roll = angles[2] * radians;
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const float sy = std::sin(yaw), cy = std::cos(yaw);
    const float sr = std::sin(roll), cr = std::cos(roll);
    return {{{cp * cy, cp * sy, -sp},
             {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp},
             {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp}}};
}

Vec3 Transform(const std::array<Vec3, 3> &axis, const Vec3 &value)
{
    Vec3 result{};
    for (std::size_t component = 0; component < 3; ++component)
        for (std::size_t direction = 0; direction < 3; ++direction)
            result[component] += axis[direction][component] * value[direction];
    return result;
}

Vec4 QuaternionFromBasis(const Vec3 &x, const Vec3 &y, const Vec3 &z)
{
    const float matrix[3][3]{{x[0], y[0], z[0]}, {x[1], y[1], z[1]}, {x[2], y[2], z[2]}};
    Vec4 quaternion{};
    const float trace = matrix[0][0] + matrix[1][1] + matrix[2][2];
    if (trace > 0.0f)
    {
        const float scale = std::sqrt(trace + 1.0f) * 2.0f;
        quaternion = {(matrix[2][1] - matrix[1][2]) / scale, (matrix[0][2] - matrix[2][0]) / scale,
                      (matrix[1][0] - matrix[0][1]) / scale, scale * 0.25f};
    }
    else
    {
        const std::size_t largest = matrix[0][0] > matrix[1][1]
                                        ? (matrix[0][0] > matrix[2][2] ? 0 : 2)
                                        : (matrix[1][1] > matrix[2][2] ? 1 : 2);
        const std::size_t next = (largest + 1) % 3;
        const std::size_t last = (largest + 2) % 3;
        const float scale =
            std::sqrt(1.0f + matrix[largest][largest] - matrix[next][next] - matrix[last][last]) *
            2.0f;
        quaternion[largest] = scale * 0.25f;
        quaternion[3] = (matrix[last][next] - matrix[next][last]) / scale;
        quaternion[next] = (matrix[next][largest] + matrix[largest][next]) / scale;
        quaternion[last] = (matrix[last][largest] + matrix[largest][last]) / scale;
    }
    return quaternion;
}

std::uint32_t PackIw3UnitVector(const Vec3 &source)
{
    const Vec3 value = Unit(source);
    std::uint32_t packed = 0;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const auto component = static_cast<std::uint32_t>(
            std::clamp(std::lround(value[axis] * 127.0f + 127.0f), 0l, 254l));
        packed |= component << (axis * 8);
    }
    return packed | 0x7F000000u;
}

void BuildPreparedStaticModels(const SourceStaticModels &source, const RenderPlan &plan,
                               const std::string &map, const std::string &assetPrefix,
                               std::vector<PreparedXModel> &xmodels,
                               replayrender::StaticModels &staticModels, const bool nativePhysics,
                               const bool preserveSourceNames = false)
{
    constexpr std::uint32_t rootBonePartBit = 0x80000000u;

    xmodels.reserve(source.models.size());
    staticModels.models.reserve(source.models.size());
    for (std::size_t modelIndex = 0; modelIndex < source.models.size(); ++modelIndex)
    {
        const SourceStaticModel &sourceModel = source.models[modelIndex];
        const std::string assetName = preserveSourceNames ? sourceModel.name
                                                          : "mw120r/" + map + "/" + assetPrefix +
                                                                "_" + std::to_string(modelIndex);
        PreparedXModel prepared;
        prepared.model.name = assetName;
        prepared.model.numBones = 1;
        prepared.model.numRootBones = 1;
        prepared.model.collLod = sourceModel.collisionLod == SIZE_MAX
                                     ? UINT8_MAX
                                     : static_cast<std::uint8_t>(sourceModel.collisionLod);
        prepared.model.shadowCutoffLod = 6;
        prepared.model.flags = 0;
        prepared.model.contents = 1;
        prepared.model.scale = 1.0f;
        prepared.model.boneNames = {"tag_origin"};
        prepared.model.skeleton.partClassification = {0};
        prepared.model.skeleton.baseMat = {{{0, 0, 0, 1}, {0, 0, 0}, 1}};

        replayrender::StaticModel staticModel;
        staticModel.name = assetName;
        replaybounds::Accumulator modelBounds;
        std::uint32_t surfaceOffset = 0;
        for (std::size_t lodIndex = 0; lodIndex < sourceModel.lods.size(); ++lodIndex)
        {
            const SourceStaticModelLod &sourceLod = sourceModel.lods[lodIndex];
            dumpsrc::XseFile xse;
            xse.loaded = true;
            xse.name = assetName + "_lod" + std::to_string(lodIndex);
            xse.modelSurfPartBits[0] = rootBonePartBit;
            std::vector<const MaterialPlan *> surfaceMaterials;
            for (const Surface &surface : sourceLod.surfaces)
            {
                const auto found = plan.materials.find(surface.material);
                if (found == plan.materials.end())
                    throw std::runtime_error("IW3 render plan omitted model material " +
                                             surface.material);
                const MaterialPlan &material = found->second;
                if (material.kind == SurfaceKind::skipped)
                    continue;
                if (material.kind == SurfaceKind::sky)
                    throw std::runtime_error("IW3 static model uses a sky material: " +
                                             sourceModel.name);
                Surface ladderRenderSurface;
                const Surface *renderSurface = &surface;
                if (sourceModel.name == "com_ladder_wood")
                {
                    ladderRenderSurface = surface;
                    AddLadderMidpointRungs(ladderRenderSurface, lodIndex);
                    renderSurface = &ladderRenderSurface;
                }
                if (renderSurface->vertices.empty() || renderSurface->vertices.size() > UINT16_MAX ||
                    renderSurface->indices.empty() || renderSurface->indices.size() % 3 ||
                    renderSurface->indices.size() / 3 > UINT16_MAX)
                {
                    throw std::runtime_error("IW3 static model has invalid surface counts: " +
                                             sourceModel.name);
                }

                dumpsrc::XseSurface converted;
                converted.partBits[0] = rootBonePartBit;
                converted.vertCount = static_cast<std::uint32_t>(renderSurface->vertices.size());
                converted.triCount = static_cast<std::uint32_t>(renderSurface->indices.size() / 3);
                converted.verticies.reserve(renderSurface->vertices.size());
                for (const Vertex &vertex : renderSurface->vertices)
                {
                    const Vec3 normal = Unit(vertex.normal);
                    Vec3 tangent =
                        Subtract(vertex.tangent, Multiply(normal, Dot(vertex.tangent, normal)));
                    if (Dot(tangent, tangent) < 1.0e-10f)
                    {
                        tangent = Cross(std::abs(normal[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0},
                                        normal);
                    }
                    tangent = Unit(tangent);

                    dumpsrc::XseVertex output;
                    std::copy_n(vertex.position.data(), 3, output.xyz);
                    output.binormalSign = vertex.binormalSign;
                    output.color = static_cast<std::uint32_t>(vertex.color[2]) |
                                   (static_cast<std::uint32_t>(vertex.color[1]) << 8) |
                                   (static_cast<std::uint32_t>(vertex.color[0]) << 16) |
                                   (static_cast<std::uint32_t>(vertex.color[3]) << 24);
                    output.texCoord =
                        (static_cast<std::uint32_t>(xsurf_conv::floatToHalf(vertex.uv[0])) << 16) |
                        xsurf_conv::floatToHalf(vertex.uv[1]);
                    output.normal = PackIw3UnitVector(normal);
                    output.tangent = PackIw3UnitVector(tangent);
                    converted.verticies.push_back(output);
                    if (lodIndex == 0)
                        modelBounds.Add(vertex.position);
                }
                converted.triIndices.reserve(renderSurface->indices.size());
                for (const std::uint32_t index : renderSurface->indices)
                {
                    if (index >= renderSurface->vertices.size())
                        throw std::runtime_error(
                            "IW3 static-model triangle references a missing vertex");
                    converted.triIndices.push_back(static_cast<std::uint16_t>(index));
                }
                converted.vertListCount = 1;
                converted.rigidVertLists.push_back(
                    {0, static_cast<std::uint16_t>(converted.vertCount), 0,
                     static_cast<std::uint16_t>(converted.triCount)});
                xse.surfaces.push_back(std::move(converted));
                surfaceMaterials.push_back(&material);
            }
            if (xse.surfaces.empty())
                throw std::runtime_error("IW3 static model LOD has no visible surfaces: " +
                                         sourceModel.name);

            conv_xsurf::Iw8Surfs converted = conv_xsurf::convert(xse);
            if (!converted.ok || converted.surfaces.size() != surfaceMaterials.size())
                throw std::runtime_error("could not convert IW3 static-model surfaces: " +
                                         sourceModel.name);

            replayrender::StaticModelLod staticLod;
            staticLod.distance = sourceLod.distance;
            for (std::size_t surfaceIndex = 0; surfaceIndex < converted.surfaces.size();
                 ++surfaceIndex)
            {
                const MaterialPlan &material = *surfaceMaterials[surfaceIndex];
                const unsigned kind = material.kind == SurfaceKind::cutout  ? 3u
                                      : material.kind == SurfaceKind::glass ? 2u
                                                                            : 0u;
                if (material.tile > UINT16_MAX || kind + material.flags > UINT8_MAX)
                    throw std::runtime_error("IW3 model material metadata exceeds Replay fields");
                const std::uint32_t metadata = static_cast<std::uint32_t>(material.tile) |
                                               ((kind + material.flags) << 16) | 0xFF000000u;
                auto &destinationSurface = converted.surfaces[surfaceIndex];
                constexpr std::size_t packedVertexSize = 20;
                constexpr std::size_t selfVisibilityOffset = 8;
                const std::size_t vertexBytes =
                    static_cast<std::size_t>(destinationSurface.vertCount) * packedVertexSize;
                if (destinationSurface.sharedVertDataOffset > converted.sharedBlob.size() ||
                    vertexBytes >
                        converted.sharedBlob.size() - destinationSurface.sharedVertDataOffset)
                {
                    throw std::runtime_error("IW3 model vertex buffer is outside shared geometry");
                }
                for (std::size_t vertex = 0; vertex < destinationSurface.vertCount; ++vertex)
                {
                    std::memcpy(converted.sharedBlob.data() +
                                    destinationSurface.sharedVertDataOffset +
                                    vertex * packedVertexSize + selfVisibilityOffset,
                                &metadata, sizeof(metadata));
                }

                if (material.modelMaterial.empty())
                    throw std::runtime_error("IW3 render plan omitted a source model material");
                const std::string &materialName = material.modelMaterial;
                prepared.model.materials.push_back(materialName);
                replayrender::StaticModelSurface staticSurface;
                std::copy_n(destinationSurface.boundsMid, 3, staticSurface.bounds.midpoint.begin());
                std::copy_n(destinationSurface.boundsHalf, 3,
                            staticSurface.bounds.halfSize.begin());
                staticSurface.material = materialName;
                staticLod.surfaces.push_back(std::move(staticSurface));
            }

            if (surfaceOffset + converted.surfaces.size() > UINT16_MAX)
                throw std::runtime_error("IW3 static model exceeds Replay's surface count");
            convert::xmodel::Iw8LodInfo lod;
            lod.dist = sourceLod.distance;
            lod.numsurfs = static_cast<std::uint16_t>(converted.surfaces.size());
            lod.surfIndex = static_cast<std::uint16_t>(surfaceOffset);
            lod.partBits[0] = rootBonePartBit;
            lod.surfsName = converted.name;
            prepared.model.lods.push_back(std::move(lod));
            surfaceOffset += static_cast<std::uint32_t>(converted.surfaces.size());
            prepared.lods.push_back(std::move(converted));
            staticModel.lods.push_back(std::move(staticLod));
        }

        const replaybounds::Bounds bounds = modelBounds.Finish();
        prepared.model.numsurfs = static_cast<std::uint16_t>(surfaceOffset);
        prepared.model.himipRadiusInvSq.reserve(prepared.model.numsurfs);
        for (const auto &lod : staticModel.lods)
            for (const auto &surface : lod.surfaces)
            {
                const float radiusSquared = std::inner_product(
                    surface.bounds.halfSize.begin(), surface.bounds.halfSize.end(),
                    surface.bounds.halfSize.begin(), 0.0f);
                prepared.model.himipRadiusInvSq.push_back(1.0f / std::max(radiusSquared, 1.0f));
            }
        prepared.model.numLods = static_cast<std::uint8_t>(prepared.model.lods.size());
        prepared.model.radius = bounds.Radius();
        std::copy_n(bounds.midpoint.data(), 3, prepared.model.boundsMid);
        std::copy_n(bounds.halfSize.data(), 3, prepared.model.boundsHalf);
        prepared.model.skeleton.boneInfo = {
            {bounds.midpoint, bounds.halfSize, prepared.model.radius * prepared.model.radius}};
        if (nativePhysics && sourceModel.collisionLod != SIZE_MAX)
        {
            iw8::havok::PhysicsMesh physics;
            std::map<Vec3, std::uint32_t> vertexIndices;
            const auto appendVertex = [&](const Vec3 &vertex) {
                const auto found = vertexIndices.find(vertex);
                if (found != vertexIndices.end())
                    return found->second;
                if (physics.vertices.size() >= std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("IW3 dynamic-model physics exceeds Replay limits");
                const auto index = static_cast<std::uint32_t>(physics.vertices.size());
                physics.vertices.push_back(vertex);
                vertexIndices.emplace(vertex, index);
                return index;
            };
            for (const Surface &surface : sourceModel.lods.at(sourceModel.collisionLod).surfaces)
                for (std::size_t first = 0; first < surface.indices.size(); first += 3)
                {
                    std::array<std::uint32_t, 3> triangle{};
                    for (std::size_t corner = 0; corner < triangle.size(); ++corner)
                    {
                        const auto sourceIndex = surface.indices.at(first + corner);
                        if (sourceIndex >= surface.vertices.size())
                            throw std::runtime_error(
                                "IW3 dynamic-model physics references a missing vertex");
                        triangle[corner] = appendVertex(surface.vertices[sourceIndex].position);
                    }
                    physics.triangles.push_back(triangle);
                }
            prepared.physicsAsset = iw8::havok::BakePhysicsAsset(assetName, physics);
            prepared.model.physicsAssetName = assetName;
        }
        staticModel.bounds = bounds;
        xmodels.push_back(std::move(prepared));
        staticModels.models.push_back(std::move(staticModel));
    }

    staticModels.instances.reserve(source.instances.size());
    for (const SourceStaticModelInstance &sourceInstance : source.instances)
    {
        std::array<Vec3, 3> axis{};
        for (std::size_t index = 0; index < axis.size(); ++index)
            axis[index] = Unit(sourceInstance.axis[index]);
        if (std::abs(Dot(axis[0], axis[1])) > 0.01f || std::abs(Dot(axis[0], axis[2])) > 0.01f ||
            std::abs(Dot(axis[1], axis[2])) > 0.01f ||
            Dot(Cross(axis[0], axis[1]), axis[2]) < 0.99f)
        {
            throw std::runtime_error("IW3 static-model instance has an invalid rotation basis");
        }
        staticModels.instances.push_back(
            {static_cast<unsigned>(sourceInstance.model), sourceInstance.origin,
             QuaternionFromBasis(axis[0], axis[1], axis[2]), sourceInstance.scale});
    }
}

Json BuildGlassPanes(const std::vector<BrushModel> &models, const RenderPlan &plan,
                     const std::string &entityText, CollisionData &collision)
{
    Json panes = Json::array();
    for (const EntityFields &entity : ParseEntityFields(entityText))
    {
        const auto classname = entity.find("classname");
        const auto modelValue = entity.find("model");
        if (classname == entity.end() || classname->second != "script_brushmodel" ||
            modelValue == entity.end() || modelValue->second.size() < 2 ||
            modelValue->second.front() != '*')
            continue;

        std::uint32_t modelIndex{};
        const char *first = modelValue->second.data() + 1;
        const char *last = modelValue->second.data() + modelValue->second.size();
        const auto parsed = std::from_chars(first, last, modelIndex);
        if (parsed.ec != std::errc{} || parsed.ptr != last || modelIndex >= models.size())
            throw std::runtime_error("IW3 glass entity references an invalid brush model");

        const BrushModel &model = models[modelIndex];
        const bool intactGlass =
            std::any_of(model.surfaces.begin(), model.surfaces.end(), [&](const Surface &surface) {
                const auto material = plan.materials.find(surface.material);
                return material != plan.materials.end() &&
                       material->second.kind == SurfaceKind::glass &&
                       surface.material.find("shattered") == std::string::npos;
            });
        if (!intactGlass)
            continue;

        Vec3 size{}, center{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            size[axis] = model.maximum[axis] - model.minimum[axis];
            center[axis] = (model.minimum[axis] + model.maximum[axis]) * 0.5f;
        }
        const std::size_t thin =
            static_cast<std::size_t>(std::min_element(size.begin(), size.end()) - size.begin());
        std::array<std::size_t, 2> plane{};
        std::size_t planeIndex = 0;
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (axis != thin)
                plane[planeIndex++] = axis;
        const float halfWidth = size[plane[0]] * 0.5f;
        const float halfHeight = size[plane[1]] * 0.5f;
        if (halfWidth < 0.125f || halfHeight < 0.125f || halfWidth * 32.0f > 32767.0f ||
            halfHeight * 32.0f > 32767.0f)
            throw std::runtime_error("IW3 glass pane dimensions exceed Replay limits");

        std::string paneMaterial;
        std::array<float, 4> texVecs{};
        Vec2 texOrigin{};
        float largestFace = 0;
        for (const Surface &surface : model.surfaces)
        {
            const auto material = plan.materials.find(surface.material);
            if (material == plan.materials.end() || material->second.glassMaterial.empty() ||
                surface.material.find("shattered") != std::string::npos)
                continue;
            for (std::size_t index = 0; index + 2 < surface.indices.size(); index += 3)
            {
                const Vertex &a = surface.vertices.at(surface.indices[index]);
                const Vertex &b = surface.vertices.at(surface.indices[index + 1]);
                const Vertex &c = surface.vertices.at(surface.indices[index + 2]);
                const float bx = b.position[plane[0]] - a.position[plane[0]];
                const float by = b.position[plane[1]] - a.position[plane[1]];
                const float cx = c.position[plane[0]] - a.position[plane[0]];
                const float cy = c.position[plane[1]] - a.position[plane[1]];
                const float determinant = bx * cy - by * cx;
                if (std::abs(determinant) <= std::max(0.0001f, largestFace))
                    continue;
                largestFace = std::abs(determinant);
                paneMaterial = material->second.glassMaterial;
                for (std::size_t channel = 0; channel < 2; ++channel)
                {
                    const float buv = b.uv[channel] - a.uv[channel];
                    const float cuv = c.uv[channel] - a.uv[channel];
                    const float dx = (buv * cy - cuv * by) / determinant;
                    const float dy = (cuv * bx - buv * cx) / determinant;
                    // FxGlassGeometryData coordinates are in 1/32 world units.
                    texVecs[channel * 2] = dx / 32.0f;
                    texVecs[channel * 2 + 1] = dy / 32.0f;
                    texOrigin[channel] = a.uv[channel] +
                        dx * (center[plane[0]] - a.position[plane[0]]) +
                        dy * (center[plane[1]] - a.position[plane[1]]);
                }
            }
        }
        if (paneMaterial.empty())
            throw std::runtime_error("IW3 glass pane has no nondegenerate textured face");

        const auto axis = EntityAxis(entity);
        const Vec3 localU{plane[0] == 0 ? 1.0f : 0.0f, plane[0] == 1 ? 1.0f : 0.0f,
                          plane[0] == 2 ? 1.0f : 0.0f};
        const Vec3 localV{plane[1] == 0 ? 1.0f : 0.0f, plane[1] == 1 ? 1.0f : 0.0f,
                          plane[1] == 2 ? 1.0f : 0.0f};
        const Vec3 worldU = Transform(axis, localU);
        const Vec3 worldV = Transform(axis, localV);
        const Vec3 worldNormal = Unit(Cross(worldU, worldV));
        Vec3 origin = Transform(axis, center);
        Vec3 entityOrigin{};
        if (const auto found = entity.find("origin"); found != entity.end())
            entityOrigin = ParseVector(found->second);
        origin = Add(origin, entityOrigin);
        if (panes.size() >= std::numeric_limits<std::uint16_t>::max())
            throw std::runtime_error("IW3 glass has too many native collision pieces");
        const auto glassId = static_cast<std::uint16_t>(panes.size() + 1);
        const std::size_t sourceHullCount = collision.hulls.size();
        std::size_t matchedHulls = 0;
        for (std::size_t hullIndex = 0; hullIndex < sourceHullCount; ++hullIndex)
        {
            auto &sourceHull = collision.hulls[hullIndex];
            if (sourceHull.model != modelIndex)
                continue;
            CollisionHull paneHull = sourceHull;
            paneHull.model = 0;
            paneHull.contents |= 0x10u;
            paneHull.glassId = glassId;
            paneHull.slabs.clear();
            for (auto &point : paneHull.points)
                point = Add(Transform(axis, point), entityOrigin);
            sourceHull.contents |= 0x10u;
            sourceHull.glassId = glassId;
            collision.hulls.push_back(std::move(paneHull));
            ++matchedHulls;
        }
        if (!matchedHulls)
            throw std::runtime_error("IW3 glass pane is missing its source collision brush");
        // FxGlassDef stores fracture extrusion in eighths of an IW world unit.
        // Keep the full brush depth for collision, but convert its half-depth for
        // the native shard geometry (an 8-unit IW3 pane becomes 0.5 here).
        const float fractureHalfThickness = std::max(0.125f, size[thin] / 16.0f);
        panes.push_back({{"material", paneMaterial},
                         {"texVecs", texVecs},
                         {"texCoordOrigin", texOrigin},
                         {"origin", origin},
                         {"quaternion", QuaternionFromBasis(worldU, worldV, worldNormal)},
                         {"halfWidth", halfWidth},
                         {"halfHeight", halfHeight},
                         {"halfThickness", fractureHalfThickness}});
    }
    return panes;
}

Json BuildRender(const Json &world, const VisibilityGroups &visibility,
                 const std::vector<BrushModel> &models, const RenderPlan &plan,
                 const std::string &entities, CollisionData &collision,
                 std::size_t &triangleCount)
{
    Json output = {{"schema", 1},
                   {"material", plan.material},
                   {"materialDefinition", plan.materialDefinition},
                   {"additionalMaterials", plan.additionalMaterials},
                   {"assetMaterials", plan.assetMaterials},
                   {"reflectionProbeArrayImage", plan.reflectionProbeArrayImage},
                   {"atlasVertexLayout", 3},
                   {"brushModels", Json::array()},
                   {"glassPanes", Json::array()},
                   {"reflectionProbes", Json::array()},
                   {"surfaces", Json::array()}};
    std::vector<std::set<unsigned>> treeSurfaces(visibility.treeBounds.size());
    std::set<unsigned> globalSurfaces;
    struct ProbeBounds
    {
        Vec3 minimum{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::infinity()};
        Vec3 maximum{-std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity()};
        bool valid{};

        void Add(const Vec3 &point)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], point[axis]);
                maximum[axis] = std::max(maximum[axis], point[axis]);
            }
            valid = true;
        }

        void Add(const Json &bounds)
        {
            if (!bounds.is_array() || bounds.size() != 2)
                throw std::runtime_error("invalid IW3 reflection-probe cell bounds");
            Add(ReadVector<3>(bounds.at(0)));
            Add(ReadVector<3>(bounds.at(1)));
        }
    };
    if (plan.reflectionProbes.empty() || plan.reflectionProbes.size() > 256)
        throw std::runtime_error("invalid IW3 reflection-probe render plan");
    std::vector<ProbeBounds> geometryProbeBounds(plan.reflectionProbes.size());
    std::vector<ProbeBounds> cellProbeBounds(plan.reflectionProbes.size());

    const auto appendSky = [&] {
        const unsigned skyMaterial = MaterialIndex(plan, SurfaceKind::sky);
        using SkyMapping = Vec3 (*)(float, float);
        const std::array<SkyMapping, 6> mappings{[](float u, float v) { return Vec3{1, -v, -u}; },
                                                 [](float u, float v) { return Vec3{-1, -v, u}; },
                                                 [](float u, float v) { return Vec3{u, 1, v}; },
                                                 [](float u, float v) { return Vec3{u, -1, -v}; },
                                                 [](float u, float v) { return Vec3{u, -v, 1}; },
                                                 [](float u, float v) { return Vec3{-u, -v, -1}; }};
        for (std::size_t face = 0; face < mappings.size(); ++face)
        {
            Json sky = {{"vertices", Json::array()},
                        {"indices", {0, 1, 2, 2, 3, 0}},
                        {"materialIndex", skyMaterial},
                        {"renderClass", "sky"}};
            std::array<Vec3, 4> positions{};
            const std::array<Vec2, 4> uvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
            for (std::size_t index = 0; index < uvs.size(); ++index)
            {
                positions[index] = Multiply(
                    mappings[face](uvs[index][0] * 2 - 1, uvs[index][1] * 2 - 1), 32768.0f);
                sky["vertices"].push_back(
                    {{"position", positions[index]},
                     {"uv", uvs[index]},
                     {"normal", 3489135616u},
                     {"lightmapUV", {static_cast<float>(plan.skyTiles[face]) + 0.25f, 1.25f}}});
            }
            if (Dot(Cross(Subtract(positions[1], positions[0]),
                          Subtract(positions[2], positions[0])),
                    positions[0]) < 0)
                sky["indices"] = {0, 2, 1, 2, 0, 3};
            output["surfaces"].push_back(std::move(sky));
            const unsigned destinationSurface =
                static_cast<unsigned>(output["surfaces"].size() - 1);
            globalSurfaces.insert(destinationSurface);
            triangleCount += 2;
        }
    };

    for (std::size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex)
    {
        const BrushModel &model = models[modelIndex];
        std::vector<const Surface *> ordered;
        ordered.reserve(model.surfaces.size());
        for (const Surface &surface : model.surfaces)
        {
            const auto found = plan.materials.find(surface.material);
            if (found == plan.materials.end())
                throw std::runtime_error("IW3 render plan omitted material " + surface.material);
            if (found->second.kind != SurfaceKind::skipped)
                ordered.push_back(&surface);
        }
        std::stable_sort(ordered.begin(), ordered.end(), [&](const Surface *a, const Surface *b) {
            const auto &ma = plan.materials.at(a->material);
            const auto &mb = plan.materials.at(b->material);
            const bool ag = ma.kind == SurfaceKind::glass;
            const bool bg = mb.kind == SurfaceKind::glass;
            if (ag != bg)
                return !ag;
            if (ma.kind != mb.kind)
                return static_cast<unsigned>(ma.kind) < static_cast<unsigned>(mb.kind);
            if (ma.environment != mb.environment)
                return ma.environment < mb.environment;
            if (a->visibilityGroup != b->visibilityGroup)
                return a->visibilityGroup < b->visibilityGroup;
            if (a->reflectionProbe != b->reflectionProbe)
                return a->reflectionProbe < b->reflectionProbe;
            return a->material < b->material;
        });

        const std::size_t firstSurface = output["surfaces"].size();
        Json *target = nullptr;
        std::size_t targetVertices = 0;
        std::size_t targetIndices = 0;
        std::string activeKey;
        bool skyAdded = modelIndex != 0;
        for (const Surface *source : ordered)
        {
            const Surface &surface = *source;
            const MaterialPlan &material = plan.materials.at(surface.material);
            if (material.kind == SurfaceKind::glass && !skyAdded)
            {
                appendSky();
                skyAdded = true;
                target = nullptr;
                activeKey.clear();
            }
            const unsigned materialIndex = MaterialIndex(plan, material.kind);
            std::ostringstream key;
            key << materialIndex;
            for (const float value : material.environment)
                key << ':' << value;
            key << ':' << surface.visibilityGroup;
            key << ':' << surface.reflectionProbe;
            if (!target || activeKey != key.str() ||
                targetVertices + surface.vertices.size() > 60000 ||
                targetIndices + surface.indices.size() > 65535u * 3u)
            {
                output["surfaces"].push_back({{"vertices", Json::array()},
                                              {"indices", Json::array()},
                                              {"materialIndex", materialIndex},
                                              {"reflectionProbe", surface.reflectionProbe},
                                              {"materialParameters", material.environment}});
                target = &output["surfaces"].back();
                targetVertices = targetIndices = 0;
                activeKey = key.str();
            }
            const unsigned destinationSurface =
                static_cast<unsigned>(output["surfaces"].size() - 1);
            if (modelIndex == 0)
            {
                if (surface.visibilityGroup == VisibilityGroups::Global)
                {
                    globalSurfaces.insert(destinationSurface);
                }
                else
                {
                    treeSurfaces.at(surface.visibilityGroup).insert(destinationSurface);
                }
            }
            std::unordered_map<std::uint32_t, std::uint32_t> remap;
            for (std::size_t offset = 0; offset < surface.indices.size(); offset += 3)
            {
                const Vertex &a = surface.vertices[surface.indices[offset]];
                const Vertex &b = surface.vertices[surface.indices[offset + 1]];
                const Vertex &c = surface.vertices[surface.indices[offset + 2]];
                const Vec3 cross =
                    Cross(Subtract(b.position, a.position), Subtract(c.position, a.position));
                if (Dot(cross, cross) < 1.0e-12f)
                    throw std::runtime_error("degenerate render triangle in IW3 map");
                for (const std::uint32_t sourceIndex :
                     {surface.indices[offset], surface.indices[offset + 2],
                      surface.indices[offset + 1]})
                {
                    auto found = remap.find(sourceIndex);
                    if (found == remap.end())
                    {
                        const Vertex &vertex = surface.vertices[sourceIndex];
                        if (surface.reflectionProbe >= geometryProbeBounds.size())
                            throw std::runtime_error(
                                "IW3 surface references an invalid reflection probe");
                        geometryProbeBounds[surface.reflectionProbe].Add(vertex.position);
                        const std::uint32_t index = static_cast<std::uint32_t>(targetVertices++);
                        const Vec2 lightmap = EncodedLightmap(vertex, surface, material, plan);
                        (*target)["vertices"].push_back(
                            {{"position", vertex.position},
                             {"uv", vertex.uv},
                             {"normal", PackedNormal(vertex.normal, vertex.tangent,
                                                     vertex.binormalSign, vertex.authoredTangent)},
                             {"lightmapUV", lightmap},
                             {"color", vertex.color}});
                        found = remap.emplace(sourceIndex, index).first;
                    }
                    (*target)["indices"].push_back(found->second);
                    ++targetIndices;
                }
                ++triangleCount;
            }
        }

        if (!skyAdded)
            appendSky();
        const std::size_t surfaceCount = output["surfaces"].size() - firstSurface;
        if (surfaceCount > 65535)
            throw std::runtime_error("IW3 brush model exceeds Replay surface limits");
        output["brushModels"].push_back({{"firstSurface", firstSurface},
                                         {"surfaceCount", surfaceCount},
                                         {"bounds", {model.minimum, model.maximum}}});
    }
    if (output["surfaces"].empty() || output["surfaces"].size() > 4096)
    {
        throw std::runtime_error("IW3 render geometry exceeds Replay surface limits");
    }
    output["dpvs"] = {{"planes", world.at("dpvs").at("planes")},
                      {"nodes", world.at("dpvs").at("nodes")},
                      {"cells", Json::array()}};
    const auto &sourceCells = world.at("dpvs").at("cells");
    for (std::size_t cellIndex = 0; cellIndex < sourceCells.size(); ++cellIndex)
    {
        const auto &source = sourceCells.at(cellIndex);
        for (const auto &probeValue : source.at("reflection_probes"))
        {
            const auto probe = probeValue.get<unsigned>();
            if (probe >= cellProbeBounds.size())
                throw std::runtime_error("IW3 cell references an invalid reflection probe");
            cellProbeBounds[probe].Add(source.at("bounds"));
        }
        Json trees = Json::array();
        for (const auto group : visibility.cellTrees.at(cellIndex))
        {
            const auto &owned = treeSurfaces.at(group);
            if (!owned.empty())
                trees.push_back({{"bounds", visibility.treeBounds.at(group)},
                                 {"surfaces", std::vector<unsigned>(owned.begin(), owned.end())}});
        }
        if (!globalSurfaces.empty())
            trees.push_back({{"bounds", source.at("bounds")},
                             {"surfaces", std::vector<unsigned>(globalSurfaces.begin(),
                                                                globalSurfaces.end())}});
        output["dpvs"]["cells"].push_back({{"bounds", source.at("bounds")},
                                           {"portals", source.at("portals")},
                                           {"trees", std::move(trees)}});
    }
    const auto &worldBounds = world.at("bounds");
    if (!worldBounds.is_array() || worldBounds.size() != 2)
        throw std::runtime_error("invalid IW3 reflection-probe world bounds");
    for (std::size_t index = 0; index < plan.reflectionProbes.size(); ++index)
    {
        const auto &probe = plan.reflectionProbes[index];
        Vec3 minimum{}, maximum{};
        if (index == 0)
        {
            minimum = ReadVector<3>(worldBounds.at(0));
            maximum = ReadVector<3>(worldBounds.at(1));
        }
        else if (geometryProbeBounds[index].valid)
        {
            minimum = geometryProbeBounds[index].minimum;
            maximum = geometryProbeBounds[index].maximum;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] -= 32.0f;
                maximum[axis] += 32.0f;
            }
        }
        else if (cellProbeBounds[index].valid)
        {
            minimum = cellProbeBounds[index].minimum;
            maximum = cellProbeBounds[index].maximum;
        }
        else
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = probe.origin[axis] - 128.0f;
                maximum[axis] = probe.origin[axis] + 128.0f;
            }
        }
        output["reflectionProbes"].push_back({{"origin", probe.origin},
                                              {"volume", {minimum, maximum}},
                                              {"image", probe.image},
                                              {"sh", probe.sh}});
    }
    output["glassPanes"] = BuildGlassPanes(models, plan, entities, collision);
    return output;
}

std::vector<Vec3> HullFromPlanes(const std::vector<Vec4> &source)
{
    if (source.size() < 4 || source.size() > 64)
    {
        throw std::runtime_error("IW3 collision brush has an invalid plane count");
    }
    std::vector<Vec4> planes;
    for (const Vec4 &plane : source)
    {
        const float length =
            std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
        if (length < 1.0e-8f)
        {
            throw std::runtime_error("IW3 collision contains a zero plane");
        }
        const Vec4 normalized{plane[0] / length, plane[1] / length, plane[2] / length,
                              plane[3] / length};
        const bool duplicate = std::any_of(planes.begin(), planes.end(), [&](const Vec4 &other) {
            return std::abs(normalized[0] - other[0]) < 1.0e-8f &&
                   std::abs(normalized[1] - other[1]) < 1.0e-8f &&
                   std::abs(normalized[2] - other[2]) < 1.0e-8f &&
                   std::abs(normalized[3] - other[3]) < 1.0e-6f;
        });
        if (!duplicate)
        {
            planes.push_back(normalized);
        }
    }

    std::array<std::array<float, 2>, 3> bounds{};
    for (auto &axis : bounds)
    {
        axis = {-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    }
    bool axial = true;
    for (const Vec4 &plane : planes)
    {
        int component = -1;
        for (int index = 0; index < 3; ++index)
        {
            if (std::abs(plane[index]) > 1.0e-8f)
            {
                if (component >= 0)
                {
                    axial = false;
                    break;
                }
                component = index;
            }
        }
        if (!axial || component < 0)
        {
            axial = false;
            break;
        }
        if (plane[component] > 0.0f)
            bounds[component][1] = std::min(bounds[component][1], plane[3] / plane[component]);
        else
            bounds[component][0] = std::max(bounds[component][0], plane[3] / plane[component]);
    }
    if (axial)
    {
        if (std::any_of(bounds.begin(), bounds.end(), [](const auto &axis) {
                return !std::isfinite(axis[0]) || !std::isfinite(axis[1]) || axis[0] >= axis[1];
            }))
        {
            throw std::runtime_error("empty or unbounded axial IW3 collision brush");
        }
        std::vector<Vec3> points;
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                for (int z = 0; z < 2; ++z)
                    points.push_back({bounds[0][x], bounds[1][y], bounds[2][z]});
        return points;
    }

    std::vector<Vec3> points;
    for (std::size_t a = 0; a < planes.size(); ++a)
        for (std::size_t b = a + 1; b < planes.size(); ++b)
            for (std::size_t c = b + 1; c < planes.size(); ++c)
            {
                const Vec3 na{planes[a][0], planes[a][1], planes[a][2]};
                const Vec3 nb{planes[b][0], planes[b][1], planes[b][2]};
                const Vec3 nc{planes[c][0], planes[c][1], planes[c][2]};
                const Vec3 bc = Cross(nb, nc);
                const float determinant = Dot(na, bc);
                if (std::abs(determinant) < 1.0e-9f)
                    continue;
                Vec3 point =
                    Add(Add(Multiply(bc, planes[a][3]), Multiply(Cross(nc, na), planes[b][3])),
                        Multiply(Cross(na, nb), planes[c][3]));
                point = Multiply(point, 1.0f / determinant);
                const bool inside = std::all_of(planes.begin(), planes.end(), [&](const Vec4 &p) {
                    return p[0] * point[0] + p[1] * point[1] + p[2] * point[2] <= p[3] + 0.002f;
                });
                const bool duplicate =
                    std::any_of(points.begin(), points.end(), [&](const Vec3 &p) {
                        const Vec3 delta = Subtract(point, p);
                        return Dot(delta, delta) < 1.0e-6f;
                    });
                if (inside && !duplicate)
                    points.push_back(point);
            }
    if (points.size() < 4 || points.size() > 252)
    {
        throw std::runtime_error("empty or invalid IW3 collision brush");
    }
    return points;
}

void CollectLeafBrushes(const Json &nodes, const std::size_t index,
                        std::vector<std::uint32_t> &brushes, std::vector<std::uint8_t> &visited)
{
    if (index >= nodes.size())
        throw std::runtime_error("IW3 collision leaf-brush child is outside its array");
    if (visited[index])
        throw std::runtime_error("IW3 collision leaf-brush tree contains a cycle");
    visited[index] = 1;

    const auto &node = nodes.at(index);
    const int count = node.at("count").get<int>();
    if (count > 0)
    {
        const auto &indices = node.at("brushes");
        if (!indices.is_array() || indices.size() != static_cast<std::size_t>(count))
            throw std::runtime_error("IW3 collision leaf-brush count is inconsistent");
        for (const auto &brush : indices)
            brushes.push_back(brush.get<std::uint32_t>());
        visited[index] = 0;
        return;
    }

    const auto &children = node.at("children");
    if (!children.is_array() || children.size() != 2)
        throw std::runtime_error("IW3 collision leaf-brush node has invalid children");

    // IW3 stores child indices relative to the current node. A negative count also
    // owns the immediately following node; zero-count nodes only use the two offsets.
    if (count < 0)
        CollectLeafBrushes(nodes, index + 1, brushes, visited);
    for (const auto &child : children)
    {
        const auto offset = child.get<std::uint32_t>();
        if (offset)
            CollectLeafBrushes(nodes, index + offset, brushes, visited);
    }
    visited[index] = 0;
}

std::uint32_t ConvertContents(const std::uint32_t source)
{
    constexpr std::uint32_t replayContents = 0x33681u;
    std::uint32_t result = source & replayContents;
    // IW3 glass and bullet-clip brushes must participate in native solid traces.
    if (source & (0x10u | 0x40u))
        result |= 1u;
    return result;
}

std::vector<CollisionHull::Slab> BuildTriggerSlabs(const std::vector<Vec3> &points,
                                                   const std::vector<Vec4> &planes)
{
    std::vector<CollisionHull::Slab> slabs;
    for (const Vec4 &plane : planes)
    {
        Vec3 direction = Unit({plane[0], plane[1], plane[2]});
        if (std::ranges::any_of(direction, [](const float component) {
                return std::abs(std::abs(component) - 1.0f) < 0.0001f;
            }))
            continue;

        const auto first = std::find_if(direction.begin(), direction.end(), [](const float value) {
            return std::abs(value) > 0.0001f;
        });
        if (first != direction.end() && *first < 0.0f)
            direction = Multiply(direction, -1.0f);

        float minimum = Dot(points.front(), direction);
        float maximum = minimum;
        for (const Vec3 &point : points)
        {
            const float projection = Dot(point, direction);
            minimum = (std::min)(minimum, projection);
            maximum = (std::max)(maximum, projection);
        }
        CollisionHull::Slab slab{direction, (minimum + maximum) * 0.5f, (maximum - minimum) * 0.5f};
        const bool duplicate = std::ranges::any_of(slabs, [&](const CollisionHull::Slab &item) {
            return std::abs(Dot(item.direction, slab.direction) - 1.0f) < 0.0001f &&
                   std::abs(item.midpoint - slab.midpoint) < 0.01f &&
                   std::abs(item.halfSize - slab.halfSize) < 0.01f;
        });
        if (!duplicate)
            slabs.push_back(slab);
    }
    return slabs;
}

CollisionData ReadCollision(const Json &collision)
{
    const auto &sourceBrushes = collision.at("brushes");
    const auto &sourceModels = collision.at("submodels");
    const auto &nodes = collision.at("leaf_brush_nodes");
    if (!sourceBrushes.is_array() || !sourceModels.is_array() || sourceModels.empty() ||
        !nodes.is_array())
        throw std::runtime_error("IW3 collision is missing brush-model topology");

    CollisionData result;
    result.models.reserve(sourceModels.size());
    std::vector<std::uint32_t> owner(sourceBrushes.size(), 0);
    for (std::size_t modelIndex = 0; modelIndex < sourceModels.size(); ++modelIndex)
    {
        const auto &model = sourceModels.at(modelIndex);
        result.models.push_back({ReadVector<3>(model.at("mins")), ReadVector<3>(model.at("maxs"))});
        if (modelIndex == 0)
            continue;

        std::vector<std::uint32_t> brushes;
        std::vector<std::uint8_t> visited(nodes.size());
        CollectLeafBrushes(nodes, model.at("leaf").get<std::size_t>(), brushes, visited);
        std::ranges::sort(brushes);
        brushes.erase(std::unique(brushes.begin(), brushes.end()), brushes.end());
        for (const auto brush : brushes)
        {
            if (brush >= owner.size())
                throw std::runtime_error("IW3 collision submodel references an invalid brush");
            if (owner[brush] && owner[brush] != modelIndex)
                throw std::runtime_error("IW3 collision brush belongs to multiple submodels");
            owner[brush] = static_cast<std::uint32_t>(modelIndex);
        }
    }

    result.hulls.reserve(sourceBrushes.size() + collision.at("triangles").size());
    for (std::size_t brushIndex = 0; brushIndex < sourceBrushes.size(); ++brushIndex)
    {
        const auto &brush = sourceBrushes.at(brushIndex);
        const std::uint32_t contents = ConvertContents(brush.at("contents").get<std::uint32_t>());
        if (!contents)
            continue;
        std::vector<Vec4> planes;
        for (const auto &plane : brush.at("planes"))
            planes.push_back(ReadVector<4>(plane));
        const Vec3 minimum = ReadVector<3>(brush.at("mins"));
        const Vec3 maximum = ReadVector<3>(brush.at("maxs"));
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            Vec4 low{};
            low[axis] = -1.0f;
            low[3] = -minimum[axis];
            planes.push_back(low);
            Vec4 high{};
            high[axis] = 1.0f;
            high[3] = maximum[axis];
            planes.push_back(high);
        }
        CollisionHull hull;
        hull.points = HullFromPlanes(planes);
        hull.slabs = BuildTriggerSlabs(hull.points, planes);
        hull.contents = contents;
        hull.model = owner.at(brushIndex);
        // IW3 records the climbable face separately from the brush planes.
        // Replay carries SURFACE_FLAG_LADDER through the low bits of the
        // Havok shape tag's userData field, so retain it in the native
        // collision intermediate instead of requiring a movement hook.
        if (const auto ladderPlanes = brush.find("ladder_planes");
            ladderPlanes != brush.end() && ladderPlanes->is_array() && !ladderPlanes->empty())
        {
            const std::size_t thinAxis = maximum[0] - minimum[0] < maximum[1] - minimum[1] ? 0 : 1;
            for (const auto &plane : *ladderPlanes)
            {
                const Vec4 value = ReadVector<4>(plane);
                if (std::abs(value[thinAxis]) >= 0.999f && std::abs(value[2]) <= 0.001f)
                    hull.ladderPlanes.push_back(value);
            }
            if (!hull.ladderPlanes.empty())
                hull.surfaceFlags |= 0x8u;
        }
        result.hulls.push_back(std::move(hull));
    }

    const auto &vertices = collision.at("vertices");
    for (const auto &triangle : collision.at("triangles"))
    {
        if (!triangle.is_array() || triangle.size() != 3)
        {
            throw std::runtime_error("invalid IW3 collision triangle");
        }
        std::array<Vec3, 3> points{};
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto index = triangle.at(corner).get<std::size_t>();
            if (index >= vertices.size())
            {
                throw std::runtime_error(
                    "IW3 collision triangle index is outside its vertex array");
            }
            points[corner] = ReadVector<3>(vertices.at(index));
        }
        const Vec3 cross = Cross(Subtract(points[1], points[0]), Subtract(points[2], points[0]));
        if (Dot(cross, cross) < 1.0e-12f)
            continue;
        const Vec3 offset = Multiply(Unit(cross), 0.125f);
        CollisionHull hull;
        hull.points.reserve(6);
        for (const float direction : {-1.0f, 1.0f})
            for (const Vec3 &point : points)
                hull.points.push_back(Add(point, Multiply(offset, direction)));
        result.hulls.push_back(std::move(hull));
    }
    constexpr std::size_t maximumHulls = 262144;
    if (result.hulls.empty() || result.hulls.size() > maximumHulls)
    {
        throw std::runtime_error("IW3 collision has " + std::to_string(result.hulls.size()) +
                                 " hulls; maximum is " + std::to_string(maximumHulls));
    }
    return result;
}

unsigned FootstepMaterial(std::string material);

void AppendStaticModelCollision(CollisionData &collision, const SourceStaticModels &staticModels)
{
    constexpr std::size_t maximumVertices = 4'000'000;
    constexpr std::size_t maximumTriangles = 4'000'000;
    CollisionMesh mesh;
    std::map<Vec3, std::uint32_t> vertexIndices;

    const auto appendVertex = [&](const Vec3 &point) {
        if (std::ranges::any_of(point, [](const float value) {
                return !std::isfinite(value) || std::abs(value) > 1000000.0f;
            }))
            throw std::runtime_error("IW3 static-model collision vertex exceeds Replay bounds");
        const auto found = vertexIndices.find(point);
        if (found != vertexIndices.end())
            return found->second;
        if (mesh.vertices.size() >= maximumVertices)
            throw std::runtime_error("IW3 static-model collision exceeds the native vertex limit");
        const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(point);
        vertexIndices.emplace(point, index);
        return index;
    };

    for (const SourceStaticModelInstance &instance : staticModels.instances)
    {
        const SourceStaticModel &model = staticModels.models.at(instance.model);
        if (model.collisionLod == SIZE_MAX)
            continue;

        const SourceStaticModelLod &lod = model.lods.at(model.collisionLod);
        for (const Surface &surface : lod.surfaces)
        {
            for (std::size_t first = 0; first < surface.indices.size(); first += 3)
            {
                std::array<Vec3, 3> triangle{};
                for (std::size_t corner = 0; corner < triangle.size(); ++corner)
                {
                    const std::size_t index = surface.indices.at(first + corner);
                    if (index >= surface.vertices.size())
                        throw std::runtime_error(
                            "IW3 static-model collision references a missing vertex");
                    triangle[corner] =
                        Add(instance.origin,
                            Multiply(Transform(instance.axis, surface.vertices[index].position),
                                     instance.scale));
                }

                const Vec3 cross =
                    Cross(Subtract(triangle[1], triangle[0]), Subtract(triangle[2], triangle[0]));
                if (Dot(cross, cross) < 1.0e-12f)
                    continue;

                if (mesh.triangles.size() >= maximumTriangles)
                    throw std::runtime_error(
                        "IW3 static-model collision exceeds the native triangle limit");
                CollisionMesh::Triangle output;
                for (std::size_t corner = 0; corner < triangle.size(); ++corner)
                    output.indices[corner] = appendVertex(triangle[corner]);
                output.material = FootstepMaterial(surface.material);
                mesh.triangles.push_back(output);
            }
        }
    }
    if (!mesh.triangles.empty())
    {
        zt::info("iw3: retained %zu placed static-model collision triangles and %zu vertices as "
                 "one native compressed mesh",
                 mesh.triangles.size(), mesh.vertices.size());
        collision.meshes.push_back(std::move(mesh));
    }
}

void WriteCollision(const std::filesystem::path &path, const CollisionData &collision)
{
    std::vector<std::uint8_t> output{'M', 'W', 'C', 'O', 'L', 'L', '0', '9'};
    const auto append = [&](const auto &value) {
        const std::size_t offset = output.size();
        output.resize(offset + sizeof(value));
        std::memcpy(output.data() + offset, &value, sizeof(value));
    };
    append(static_cast<std::uint32_t>(collision.hulls.size()));
    append(static_cast<std::uint32_t>(collision.models.size()));
    append(static_cast<std::uint32_t>(collision.meshes.size()));
    for (const auto &model : collision.models)
    {
        for (const auto value : model.minimum)
            append(value);
        for (const auto value : model.maximum)
            append(value);
    }
    for (const auto &hull : collision.hulls)
    {
        append(static_cast<std::uint32_t>(hull.points.size()));
        append(hull.contents);
        append(hull.model);
        append(static_cast<std::uint32_t>(hull.slabs.size()));
        append(hull.surfaceFlags);
        append(static_cast<std::uint32_t>(hull.ladderPlanes.size()));
        append(hull.glassId);
        for (const Vec3 &point : hull.points)
        {
            for (const float value : point)
            {
                if (!std::isfinite(value) || std::abs(value) > 1000000.0f)
                {
                    throw std::runtime_error("IW3 collision vertex exceeds Replay bounds");
                }
                append(value);
            }
        }
        for (const auto &slab : hull.slabs)
        {
            for (const float value : slab.direction)
                append(value);
            append(slab.midpoint);
            append(slab.halfSize);
        }
        for (const auto &plane : hull.ladderPlanes)
            for (const float value : plane)
                append(value);
    }
    for (const auto &mesh : collision.meshes)
    {
        append(static_cast<std::uint32_t>(mesh.vertices.size()));
        append(static_cast<std::uint32_t>(mesh.triangles.size()));
        append(mesh.model);
        for (const Vec3 &point : mesh.vertices)
            for (const float value : point)
                append(value);
        for (const auto &triangle : mesh.triangles)
        {
            for (const auto index : triangle.indices)
                append(index);
            append(triangle.contents);
            append(triangle.surfaceFlags);
            append(triangle.material);
        }
    }
    if (!zt::write_file(path.string(), output))
    {
        throw std::runtime_error("cannot write temporary Replay collision");
    }
}

std::string EntityValue(const std::string &entities, const std::string &key)
{
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyStart = entities.find(needle);
    if (keyStart == std::string::npos)
        return {};
    const std::size_t valueStart = entities.find('"', keyStart + needle.size());
    if (valueStart == std::string::npos)
        return {};
    const std::size_t valueEnd = entities.find('"', valueStart + 1);
    if (valueEnd == std::string::npos)
        return {};
    return entities.substr(valueStart + 1, valueEnd - valueStart - 1);
}

Vec3 ParseEntityVector(const std::string &value, const Vec3 &fallback)
{
    if (value.empty())
        return fallback;
    std::istringstream input(value);
    Vec3 result{};
    if (!(input >> result[0] >> result[1] >> result[2]) ||
        std::any_of(result.begin(), result.end(), [](float item) { return !std::isfinite(item); }))
    {
        throw std::runtime_error("invalid IW3 worldspawn vector");
    }
    return result;
}

float ParseEntityFloat(const std::string &value, const float fallback)
{
    if (value.empty())
        return fallback;
    std::size_t consumed = 0;
    const float result = std::stof(value, &consumed);
    if (consumed != value.size() || !std::isfinite(result) || result < 0.0f)
    {
        throw std::runtime_error("invalid IW3 worldspawn sunlight");
    }
    return result;
}

Json BuildLighting(const Json &world, const Json &commonWorld, const std::string &entities)
{
    const Json &sun = world.at("sun");
    const Vec3 exportedAngles = ReadVector<3>(sun.at("angles"));
    const Vec3 angles = ParseEntityVector(EntityValue(entities, "sundirection"), exportedAngles);
    constexpr float degrees = 3.14159265358979323846f / 180.0f;
    const float pitch = angles[0] * degrees;
    const float yaw = angles[1] * degrees;
    const Vec3 direction{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
                         -std::sin(pitch)};

    Vec3 fallbackColor = ReadVector<3>(sun.at("color"));
    if (*std::max_element(fallbackColor.begin(), fallbackColor.end()) <= 0.0f)
    {
        fallbackColor = ReadVector<3>(sun.at("ambient"));
        const float peak = *std::max_element(fallbackColor.begin(), fallbackColor.end());
        if (peak > 0.0f)
            fallbackColor = Multiply(fallbackColor, 1.0f / peak);
    }
    const Vec3 color = ParseEntityVector(EntityValue(entities, "suncolor"), fallbackColor);
    if (std::any_of(color.begin(), color.end(), [](float value) { return value < 0.0f; }))
    {
        throw std::runtime_error("invalid IW3 worldspawn sun color");
    }
    const float intensity =
        ParseEntityFloat(EntityValue(entities, "sunlight"), sun.at("intensity").get<float>());
    return {{"schema", 1},
            {"intensity", intensity},
            {"color", color},
            {"direction", Unit(direction)},
            {"up", {0.0f, 0.0f, 0.0f}},
            {"sun_primary_light_index", world.at("sun_primary_light_index")},
            {"primary_lights", commonWorld.at("primary_lights")}};
}

void CopyCompass(const std::filesystem::path &extracted, const std::filesystem::path &prepared,
                 const std::string &sourceMap, const std::string &targetMap)
{
    const std::string sourceStem = "compass_map_" + sourceMap;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(extracted, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        std::string extension = iterator->path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](const unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (!iterator->is_regular_file(error) || (extension != ".dds" && extension != ".iwi") ||
            _stricmp(iterator->path().stem().string().c_str(), sourceStem.c_str()) != 0)
            continue;
        const auto destination = prepared / "images" / ("compass_map_" + targetMap + extension);
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(iterator->path(), destination,
                                   std::filesystem::copy_options::overwrite_existing);
        zt::info("iw3: included HUD minimap %s", destination.filename().string().c_str());
        return;
    }
}

unsigned FootstepMaterial(std::string material)
{
    std::ranges::transform(material, material.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    const auto contains = [&material](const std::string_view token) {
        return material.find(token) != std::string::npos;
    };
    // Replay's movement sound picker uses these native surface-type values.
    // Keep PM_Concrete (5) as the safe fallback for unclassified geometry.
    if (contains("carpet") || contains("rug") || contains("fabric"))
        return 3;
    if (contains("wood") || contains("plank") || contains("timber"))
        return 22;
    if (contains("metal") || contains("steel") || contains("iron") || contains("diamond"))
        return 13;
    if (contains("grass") || contains("foliage"))
        return 10;
    if (contains("dirt") || contains("mud"))
        return 6;
    if (contains("sand"))
        return 18;
    if (contains("gravel"))
        return 11;
    if (contains("rock") || contains("stone"))
        return 17;
    if (contains("snow"))
        return 19;
    if (contains("ice"))
        return 12;
    if (contains("glass"))
        return 9;
    if (contains("brick"))
        return 2;
    if (contains("plaster"))
        return 16;
    if (contains("paper"))
        return 15;
    return 5;
}

void WriteFootsteps(const std::filesystem::path &path, const std::vector<BrushModel> &models)
{
    // MWRSTEP1 stores world-space triangles. Only upward-facing faces are
    // walkable; walls, ceilings, and degenerate faces are excluded.
    std::vector<std::array<float, 10>> triangles;
    if (!models.empty())
    {
        for (const auto &surface : models.front().surfaces)
        {
            const unsigned material = FootstepMaterial(surface.material);
            for (std::size_t offset = 0; offset < surface.indices.size(); offset += 3)
            {
                const auto &a = surface.vertices.at(surface.indices[offset]);
                const auto &b = surface.vertices.at(surface.indices[offset + 1]);
                const auto &c = surface.vertices.at(surface.indices[offset + 2]);
                const Vec3 normal =
                    Cross(Subtract(b.position, a.position), Subtract(c.position, a.position));
                const float length = std::sqrt(Dot(normal, normal));
                if (!std::isfinite(length) || length < 1.0e-5f || normal[2] / length < 0.55f)
                    continue;
                std::array<float, 10> triangle{a.position[0], a.position[1],
                                               a.position[2], b.position[0],
                                               b.position[1], b.position[2],
                                               c.position[0], c.position[1],
                                               c.position[2], static_cast<float>(material)};
                if (std::ranges::any_of(triangle, [](const float value) {
                        return !std::isfinite(value) || std::abs(value) > 100000.0f;
                    }))
                    throw std::runtime_error("IW3 footstep triangle contains invalid coordinates");
                triangles.push_back(triangle);
                if (triangles.size() > 600000)
                    throw std::runtime_error("IW3 walkable surface count exceeds Replay limits");
            }
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot write temporary IW3 footsteps");
    output.write("MWRSTEP1", 8);
    const auto count = static_cast<std::uint32_t>(triangles.size());
    output.write(reinterpret_cast<const char *>(&count), sizeof(count));
    for (const auto &triangle : triangles)
    {
        output.write(reinterpret_cast<const char *>(triangle.data()), 9 * sizeof(float));
        const auto material = static_cast<std::uint32_t>(triangle[9]);
        output.write(reinterpret_cast<const char *>(&material), sizeof(material));
    }
    if (!output)
        throw std::runtime_error("cannot finish temporary IW3 footsteps");
    zt::info("iw3: generated %u walkable footstep triangles", count);
}
} // namespace

PreparedMap::PreparedMap(PreparedMap &&other) noexcept
    : root(std::move(other.root))
    , collision(std::move(other.collision))
    , footsteps(std::move(other.footsteps))
    , scratch(std::move(other.scratch))
    , xmodels(std::move(other.xmodels))
    , fxEffects(std::move(other.fxEffects))
    , staticModels(std::move(other.staticModels))
    , dynamicEntities(std::move(other.dynamicEntities))
{
    other.scratch.clear();
}

PreparedMap &PreparedMap::operator=(PreparedMap &&other) noexcept
{
    if (this != &other)
    {
        if (!scratch.empty())
        {
            std::error_code error;
            std::filesystem::remove_all(scratch, error);
        }
        root = std::move(other.root);
        collision = std::move(other.collision);
        footsteps = std::move(other.footsteps);
        scratch = std::move(other.scratch);
        xmodels = std::move(other.xmodels);
        fxEffects = std::move(other.fxEffects);
        staticModels = std::move(other.staticModels);
        dynamicEntities = std::move(other.dynamicEntities);
        other.scratch.clear();
    }
    return *this;
}

PreparedMap::~PreparedMap()
{
    if (!scratch.empty())
    {
        std::error_code error;
        std::filesystem::remove_all(scratch, error);
    }
}

PreparedMap PrepareFastfile(const ImportOptions &options)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(options.fastfile, error) ||
        options.fastfile.extension() != ".ff")
    {
        throw std::runtime_error("IW3 input must be a readable .ff file");
    }

    PreparedMap result;
    result.scratch = MakeScratchDirectory(options.scratchRoot);
    const auto extracted = result.scratch / "extracted";
    result.root = result.scratch / "prepared";
    std::filesystem::create_directories(extracted);
    std::filesystem::create_directories(result.root);

    const auto unlinker = FindUnlinker(options.unlinker);
    zt::info("iw3: reading %s with %s", options.fastfile.string().c_str(),
             unlinker.string().c_str());
    RunUnlinker(unlinker, options, extracted);

    const auto worldPath = FindSingleExport(extracted, ".replay-world.json");
    const auto collisionPath = FindSingleExport(extracted, ".replay-collision.json");
    const auto commonWorldPath = FindSingleExport(extracted, ".replay-comworld.json");
    const auto exportRoot = worldPath.parent_path().parent_path().parent_path();
    const std::string sourceAsset = worldPath.filename().string().substr(
        0, worldPath.filename().string().size() - std::string(".replay-world.json").size());
    constexpr std::string_view bspSuffix = ".d3dbsp";
    const std::string sourceMap = sourceAsset.ends_with(bspSuffix)
                                      ? sourceAsset.substr(0, sourceAsset.size() - bspSuffix.size())
                                      : sourceAsset;
    const Json world = ReadJson(worldPath);
    const Json collision = ReadJson(collisionPath);
    const Json commonWorld = ReadJson(commonWorldPath);
    if (world.at("schema") != 1 || collision.at("schema") != 1 || commonWorld.at("schema") != 1)
    {
        throw std::runtime_error("unsupported ReplayMapDumpers export schema");
    }
    if (world.at("name").get<std::string>() != collision.at("name").get<std::string>() ||
        world.at("name").get<std::string>() != commonWorld.at("name").get<std::string>())
    {
        throw std::runtime_error(
            "IW3 world, collision, and common-world exports name different maps");
    }
    const auto &sourceLights = commonWorld.at("primary_lights");
    const auto primaryLightCount = world.at("primary_light_count").get<std::size_t>();
    const auto sunPrimaryLightIndex = world.at("sun_primary_light_index").get<std::size_t>();
    if (!sourceLights.is_array() || sourceLights.size() != primaryLightCount ||
        primaryLightCount < 2 || primaryLightCount > 65535 ||
        sunPrimaryLightIndex >= primaryLightCount)
    {
        throw std::runtime_error("invalid IW3 primary-light table");
    }

    std::string entities;
    const auto sourceEntities = worldPath.parent_path() / (sourceAsset + ".ents");
    if (!zt::read_file_str(sourceEntities.string(), entities) || entities.empty())
    {
        throw std::runtime_error("IW3 map entity export is missing");
    }

    const auto entityModelNames = convert::iw3ReplayEntityModels(entities);
    auto missingEntityModels = MissingEntityModels(exportRoot, entityModelNames);
    if (!missingEntityModels.empty())
    {
        const auto commonFastfile = FindIw3CommonFastfile(options);
        if (commonFastfile.empty())
        {
            throw std::runtime_error(
                "IW3 entity model assets are absent from the map fastfile (" +
                JoinNames(missingEntityModels) +
                "); add the IW3 installation root or zone/english directory with --search-path");
        }
        zt::info("iw3: resolving %zu entity model asset(s) from %s", missingEntityModels.size(),
                 commonFastfile.string().c_str());
        RunUnlinker(unlinker, options, exportRoot, commonFastfile, true);
        missingEntityModels = MissingEntityModels(exportRoot, entityModelNames);
        if (!missingEntityModels.empty())
        {
            throw std::runtime_error("IW3 entity model assets were not found in common_mp.ff: " +
                                     JoinNames(missingEntityModels));
        }
    }

    // The ordinary world extraction can omit map-local FX payloads. Read those
    // from the map fastfile first, then resolve only still-missing declarations
    // from the shared multiplayer zone.
    RunUnlinker(unlinker, options, exportRoot, options.fastfile, true);
    const auto missingFx = MissingDeclaredFx(exportRoot);
    if (!missingFx.empty())
    {
        const auto commonFastfile = FindIw3CommonFastfile(options);
        if (!commonFastfile.empty())
        {
            zt::info("iw3: resolving %zu shared source FX graph(s) from %s",
                     missingFx.size(), commonFastfile.string().c_str());
            RunUnlinker(unlinker, options, exportRoot, commonFastfile, true);
        }
    }
    const auto unresolvedFx = MissingDeclaredFx(exportRoot);
    if (!unresolvedFx.empty())
        zt::warn("iw3: %zu declared source FX graph(s) have no exported payload; first: %s",
                 unresolvedFx.size(), unresolvedFx.front().c_str());

    AuditSourceAssetDeclarations(exportRoot);
    result.fxEffects = ReadPreparedFx(exportRoot);
    const auto declaredFx = ReadDeclaredFx(exportRoot / "zone_source" / (sourceMap + ".zone"));
    std::set<std::string> reachableFx(declaredFx.begin(), declaredFx.end());
    std::vector<std::string> pendingFx(declaredFx.begin(), declaredFx.end());
    for (std::size_t index = 0; index < pendingFx.size(); ++index)
    {
        const auto &name = pendingFx[index];
        const auto graph = std::find_if(result.fxEffects.begin(), result.fxEffects.end(),
                                        [&name](const PreparedFx &effect) {
                                            return effect.name == name;
                                        });
        if (graph == result.fxEffects.end())
            throw std::runtime_error("map-reachable IW3 FX graph is missing: " + name +
                                     "; add the matching IW3 zone with --search-path");
        for (const auto &dependency : graph->dependencies)
            if (dependency.type == "fx" && reachableFx.insert(dependency.name).second)
                pendingFx.push_back(dependency.name);
    }
    std::erase_if(result.fxEffects, [&reachableFx](const PreparedFx &effect) {
        return !reachableFx.contains(effect.name);
    });
    zt::info("iw3: collected %zu map-reachable source FX graph(s) from %zu declaration(s)",
             result.fxEffects.size(), declaredFx.size());
    std::set<std::string> fxMaterialNames;
    for (const auto &effect : result.fxEffects)
        for (const auto &dependency : effect.dependencies)
            if (dependency.type == "material")
                fxMaterialNames.insert(dependency.name);
    std::vector<std::string> missingFxMaterials;
    for (const auto &name : fxMaterialNames)
    {
        std::error_code materialError;
        if (!std::filesystem::is_regular_file(exportRoot / "materials" / (name + ".json"),
                                              materialError))
            missingFxMaterials.push_back(name);
    }
    if (!missingFxMaterials.empty())
        throw std::runtime_error("map-reachable IW3 FX material source is missing: " +
                                 JoinNames(missingFxMaterials) +
                                 "; add the matching IW3 main and zone directories with --search-path");
    zt::info("iw3: verified %zu distinct FX material source export(s)",
             fxMaterialNames.size());
    bool localLinearLightDef = false;
    for (std::size_t index = 0; index < sourceLights.size(); ++index)
        if (index != sunPrimaryLightIndex &&
            sourceLights.at(index).value("definition", std::string{}) == "light_point_linear")
            localLinearLightDef = true;
    if (localLinearLightDef)
    {
        std::string sourceLightDef;
        const auto path = exportRoot / "lights" / "light_point_linear";
        std::string expected(1, static_cast<char>(0x62));
        expected.append("falloff_linear", 14);
        expected.push_back('\0');
        if (!zt::read_file_str(path.string(), sourceLightDef) || sourceLightDef != expected)
            throw std::runtime_error(
                "map-local IW3 light_point_linear needs its exact attenuation export; "
                "add the matching IW3 zone with --search-path");
        zt::info("iw3: matched map-local light_point_linear to Replay's native type-34 definition");
    }
    const auto glassFx = std::find_if(result.fxEffects.begin(), result.fxEffects.end(),
                                      [](const PreparedFx &effect) {
                                          return effect.name == "impacts/small_glass";
                                      });
    if (glassFx != result.fxEffects.end())
    {
        std::vector<std::string> models;
        for (const auto &dependency : glassFx->dependencies)
            if (dependency.type == "xmodel")
                models.push_back(dependency.name);
        const auto missing = MissingEntityModels(exportRoot, models);
        zt::info("iw3: small_glass has %zu source dependency(s), %zu model shard(s), %zu missing model export(s)",
                 glassFx->dependencies.size(), models.size(), missing.size());
        if (!missing.empty())
            zt::warn("iw3: small_glass shard model export missing: %s", JoinNames(missing).c_str());
    }

    const VisibilityGroups visibility = ReadVisibilityGroups(world);
    SourceStaticModels sourceStaticModels;
    std::vector<BrushModel> brushModels =
        ReadBrushModels(world, exportRoot, visibility, sourceStaticModels);
    SourceDynamicEntities sourceDynamicEntities = ReadDynamicEntities(collision, exportRoot);
    SourceStaticModels sourceEntityModels;
    sourceEntityModels.models.reserve(entityModelNames.size());
    for (const auto &modelName : entityModelNames)
        sourceEntityModels.models.push_back(ReadSourceModel(exportRoot, modelName));
    SourceStaticModels sourceFxModels;
    std::set<std::string> fxModelNames;
    for (const auto &effect : result.fxEffects)
        for (const auto &dependency : effect.dependencies)
            if (dependency.type == "xmodel")
                fxModelNames.insert(dependency.name);
    for (const auto &model : sourceEntityModels.models)
        fxModelNames.erase(model.name);
    sourceFxModels.models.reserve(fxModelNames.size());
    for (const auto &name : fxModelNames)
        sourceFxModels.models.push_back(ReadSourceModel(exportRoot, name));
    std::vector<std::string> materialNames;
    std::vector<std::string> modelMaterialNames;
    for (const BrushModel &model : brushModels)
        for (const Surface &surface : model.surfaces)
            materialNames.push_back(surface.material);
    for (const SourceStaticModel &model : sourceStaticModels.models)
        for (const SourceStaticModelLod &lod : model.lods)
            for (const Surface &surface : lod.surfaces)
            {
                materialNames.push_back(surface.material);
                modelMaterialNames.push_back(surface.material);
            }
    for (const SourceStaticModel &model : sourceDynamicEntities.models.models)
        for (const SourceStaticModelLod &lod : model.lods)
            for (const Surface &surface : lod.surfaces)
            {
                materialNames.push_back(surface.material);
                modelMaterialNames.push_back(surface.material);
            }
    for (const SourceStaticModel &model : sourceEntityModels.models)
        for (const SourceStaticModelLod &lod : model.lods)
            for (const Surface &surface : lod.surfaces)
            {
                materialNames.push_back(surface.material);
                modelMaterialNames.push_back(surface.material);
            }
    for (const SourceStaticModel &model : sourceFxModels.models)
        for (const SourceStaticModelLod &lod : model.lods)
            for (const Surface &surface : lod.surfaces)
            {
                materialNames.push_back(surface.material);
                modelMaterialNames.push_back(surface.material);
            }
    const auto mapDirectory = result.root / "maps" / "mp";
    std::filesystem::create_directories(mapDirectory);
    const RenderPlan renderPlan =
        PrepareRenderAssets(exportRoot, world, materialNames, modelMaterialNames,
                            options.searchPaths, mapDirectory, options.map);
    BuildPreparedStaticModels(sourceStaticModels, renderPlan, options.map, "smodel", result.xmodels,
                              result.staticModels, true);
    replayrender::StaticModels unusedDynamicTables;
    BuildPreparedStaticModels(sourceDynamicEntities.models, renderPlan, options.map, "dynmodel",
                              result.xmodels, unusedDynamicTables, true);
    BuildPreparedStaticModels(sourceEntityModels, renderPlan, options.map, "entitymodel",
                              result.xmodels, unusedDynamicTables, true, true);
    replayrender::StaticModels unusedFxTables;
    BuildPreparedStaticModels(sourceFxModels, renderPlan, options.map, "fxmodel",
                              result.xmodels, unusedFxTables, true, true);
    zt::info("iw3: converted %zu FX-referenced XModels through native model/material closure",
             sourceFxModels.models.size());
    result.dynamicEntities.reserve(sourceDynamicEntities.definitionModels.size() +
                                   sourceDynamicEntities.brushes.size());
    for (std::size_t index = 0; index < sourceDynamicEntities.definitionModels.size(); ++index)
    {
        const std::size_t modelIndex = sourceDynamicEntities.definitionModels[index];
        const bool noPhysics =
            sourceDynamicEntities.models.models.at(modelIndex).collisionLod == SIZE_MAX;
        result.dynamicEntities.push_back(
            {"mw120r/" + options.map + "/dynmodel_" +
                 std::to_string(modelIndex),
             sourceDynamicEntities.quaternions[index], sourceDynamicEntities.origins[index],
             noPhysics});
    }
    for (const auto &source : sourceDynamicEntities.brushes)
    {
        iw8::DynamicEntity entity;
        entity.quaternion = source.quaternion;
        entity.origin = source.origin;
        entity.noPhysics = false;
        entity.basis = iw8::DynamicEntityBasis::brush;
        entity.brushModel = source.model;
        result.dynamicEntities.push_back(std::move(entity));
    }
    std::size_t triangles = 0;
    auto nativeCollision = ReadCollision(collision);
    const Json render = BuildRender(world, visibility, brushModels, renderPlan, entities,
                                    nativeCollision, triangles);
    result.footsteps = result.scratch / "footsteps.native";
    WriteFootsteps(result.footsteps, brushModels);

    const std::string targetAsset = options.map + ".d3dbsp";
    WriteJson(mapDirectory / (targetAsset + ".render.json"), render);
    WriteJson(mapDirectory / (targetAsset + ".lighting.json"),
              BuildLighting(world, commonWorld, entities));
    PrepareNativeLightGrid(exportRoot, sourceAsset,
                           mapDirectory / (targetAsset + ".gpulightgrid.native"));

    const auto worldBounds = world.at("bounds");
    if (!worldBounds.is_array() || worldBounds.size() != 2)
    {
        throw std::runtime_error("invalid IW3 world bounds");
    }
    Vec3 minimum = ReadVector<3>(worldBounds.at(0));
    Vec3 maximum = ReadVector<3>(worldBounds.at(1));
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        minimum[axis] -= 64.0f;
        maximum[axis] += 64.0f;
    }
    WriteJson(mapDirectory / (targetAsset + ".bounds.json"),
              {{"schema", 1}, {"min", minimum}, {"max", maximum}});

    if (!zt::write_file_str((mapDirectory / (targetAsset + ".ents")).string(), entities))
    {
        throw std::runtime_error("cannot write temporary IW3 entities");
    }
    result.collision = result.scratch / "collision.native";
    WriteCollision(result.collision, nativeCollision);
    CopyCompass(extracted, result.root, sourceMap, options.map);

    zt::info("iw3: normalized %zu triangles, %zu collision hulls, %zu brush models, %zu static "
             "models and %zu dynamic models",
             triangles, nativeCollision.hulls.size(), nativeCollision.models.size(),
             sourceStaticModels.instances.size(), result.dynamicEntities.size());
    zt::info("iw3: preserved %zu source materials and %zu authored lightmaps",
             renderPlan.materials.size(), renderPlan.lightmaps.size());
    std::size_t portalCount = 0;
    for (const auto &cell : world.at("dpvs").at("cells"))
        portalCount += cell.at("portals").size();
    zt::info(
        "iw3: preserved %zu DPVS cells, %zu AABB trees (%zu populated), %zu planes and %zu portals",
        visibility.cellTrees.size(), visibility.sourceTreeCount, visibility.treeBounds.size(),
        world.at("dpvs").at("planes").size(), portalCount);
    return result;
}
} // namespace iw3
