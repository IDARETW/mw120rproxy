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
#include <optional>
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
    std::uint32_t materialOverride{};
    std::vector<Vec4> ladderPlanes;
    // Per face: world-space tangent coordinate of the model center, then width.
    std::vector<std::array<float, 2>> ladderModelTangents;
    float ladderRungOffset{};
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
    std::vector<std::vector<std::uint32_t>> treeModels;
    std::vector<std::vector<std::uint32_t>> groupTrees;
    std::vector<std::uint16_t> treeChildCounts;
    std::vector<std::uint32_t> treeFirstChildren;
    std::vector<std::vector<std::uint32_t>> cellTrees;
    bool nativeTopology{};
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
        arguments.push_back(L"xmodel,material,image,fx,impactfx");
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

void CopyExtractedEntityModels(const std::filesystem::path &destination,
                               const std::vector<std::string> &models,
                               const std::vector<std::filesystem::path> &searchPaths)
{
    for (const std::string &model : models)
    {
        for (const auto &searchPath : searchPaths)
        {
            const auto sourceModel = searchPath / "xmodel" / (model + ".json");
            std::error_code error;
            if (!std::filesystem::is_regular_file(sourceModel, error))
                continue;

            const auto targetModel = destination / "xmodel" / sourceModel.filename();
            std::filesystem::create_directories(targetModel.parent_path(), error);
            if (error)
                throw std::runtime_error("cannot create IW3 entity-model staging directory");
            std::filesystem::copy_file(sourceModel, targetModel,
                                       std::filesystem::copy_options::overwrite_existing, error);
            if (error)
                throw std::runtime_error("cannot stage IW3 entity model " + model);

            const auto sourceAttributes = searchPath / "xmodel" / (model + ".replay.json");
            if (!std::filesystem::is_regular_file(sourceAttributes, error))
                continue;
            std::filesystem::copy_file(sourceAttributes, destination / "xmodel" /
                                                            sourceAttributes.filename(),
                                       std::filesystem::copy_options::overwrite_existing, error);
            if (error)
                throw std::runtime_error("cannot stage IW3 entity-model attributes " + model);

            const auto sourceModels = searchPath / "model_export";
            if (!std::filesystem::is_directory(sourceModels, error))
                continue;
            const auto targetModels = destination / "model_export";
            std::filesystem::create_directories(targetModels, error);
            if (error)
                throw std::runtime_error("cannot create IW3 model-export staging directory");
            for (std::filesystem::directory_iterator entry(sourceModels, error), end;
                 !error && entry != end; entry.increment(error))
            {
                if (!entry->is_regular_file(error) ||
                    !entry->path().filename().string().starts_with(model))
                    continue;
                std::filesystem::copy_file(entry->path(), targetModels / entry->path().filename(),
                                           std::filesystem::copy_options::overwrite_existing, error);
                if (error)
                    throw std::runtime_error("cannot stage IW3 entity-model mesh " + model);
            }
            if (error)
                throw std::runtime_error("cannot enumerate IW3 entity-model mesh files");
            zt::info("iw3: staged entity model %s from extracted search assets", model.c_str());
            break;
        }
    }
}

void CopyExtractedFx(const std::filesystem::path &destination,
                     const std::vector<std::filesystem::path> &searchPaths)
{
    std::size_t copied = 0;
    for (const auto &searchPath : searchPaths)
    {
        const auto sourceRoot = searchPath / "fx";
        std::error_code error;
        if (!std::filesystem::is_directory(sourceRoot, error))
            continue;
        for (std::filesystem::recursive_directory_iterator entry(sourceRoot, error), end;
             !error && entry != end; entry.increment(error))
        {
            if (!entry->is_regular_file(error) || entry->path().extension() != ".json")
                continue;
            const auto target = destination / "fx" /
                                std::filesystem::relative(entry->path(), sourceRoot, error);
            if (error)
                throw std::runtime_error("cannot resolve extracted IW3 FX asset path");
            std::filesystem::create_directories(target.parent_path(), error);
            if (error)
                throw std::runtime_error("cannot create IW3 FX staging directory");
            std::filesystem::copy_file(entry->path(), target,
                                       std::filesystem::copy_options::skip_existing, error);
            if (error)
                throw std::runtime_error("cannot stage extracted IW3 FX source asset");
            ++copied;
        }
        if (error)
            throw std::runtime_error("cannot enumerate extracted IW3 FX source assets");
    }
    if (copied)
        zt::info("iw3: staged %zu extracted source FX graph(s)", copied);
}

void CopyExtractedFxMaterials(const std::filesystem::path &destination,
                              const std::vector<std::filesystem::path> &searchPaths)
{
    std::size_t copied = 0;
    for (const auto &searchPath : searchPaths)
    {
        const auto sourceRoot = searchPath / "materials";
        std::error_code error;
        if (!std::filesystem::is_directory(sourceRoot, error))
            continue;
        for (std::filesystem::recursive_directory_iterator entry(sourceRoot, error), end;
             !error && entry != end; entry.increment(error))
        {
            if (!entry->is_regular_file(error) || entry->path().extension() != ".json")
                continue;
            const auto target = destination / "materials" /
                                std::filesystem::relative(entry->path(), sourceRoot, error);
            if (error)
                throw std::runtime_error("cannot resolve extracted IW3 FX material path");
            std::filesystem::create_directories(target.parent_path(), error);
            if (error)
                throw std::runtime_error("cannot create IW3 FX material staging directory");
            std::filesystem::copy_file(entry->path(), target,
                                       std::filesystem::copy_options::skip_existing, error);
            if (error)
                throw std::runtime_error("cannot stage extracted IW3 FX material source");
            ++copied;
        }
        if (error)
            throw std::runtime_error("cannot enumerate extracted IW3 FX material sources");
    }
    if (copied)
        zt::info("iw3: staged %zu extracted source FX material(s)", copied);
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

bool IsValidFxAssetName(const std::string &name)
{
    if (name.empty() || name.size() > 256 || name.find('\0') != std::string::npos)
        return false;
    const std::filesystem::path path(name);
    if (path.has_root_path())
        return false;
    for (const auto &part : path)
        if (part == "." || part == "..")
            return false;
    return true;
}

template <typename T>
T FxInteger(const Json &value, const char *field)
{
    if (!value.is_number_integer() && !value.is_number_unsigned())
        throw std::runtime_error(std::string("IW3 FX field is not an integer: ") + field);
    const auto number = value.get<std::int64_t>();
    if (number < static_cast<std::int64_t>(std::numeric_limits<T>::lowest()) ||
        number > static_cast<std::int64_t>(std::numeric_limits<T>::max()))
        throw std::runtime_error(std::string("IW3 FX integer is out of range: ") + field);
    return static_cast<T>(number);
}

float FxFloat(const Json &value, const char *field)
{
    if (!value.is_number())
        throw std::runtime_error(std::string("IW3 FX field is not numeric: ") + field);
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max())
        throw std::runtime_error(std::string("IW3 FX float is invalid: ") + field);
    return static_cast<float>(number);
}

template <std::size_t Count>
std::array<float, Count> FxFloatArray(const Json &value, const char *field)
{
    if (!value.is_array() || value.size() != Count)
        throw std::runtime_error(std::string("IW3 FX array has the wrong size: ") + field);
    std::array<float, Count> result{};
    for (std::size_t index = 0; index < Count; ++index)
        result[index] = FxFloat(value.at(index), field);
    return result;
}

PreparedFxFloatRange FxFloatRange(const Json &value, const char *field)
{
    if (!value.is_object())
        throw std::runtime_error(std::string("IW3 FX range is not an object: ") + field);
    return {FxFloat(value.at("base"), field), FxFloat(value.at("amplitude"), field)};
}

PreparedFxIntRange FxIntRange(const Json &value, const char *field)
{
    if (!value.is_object())
        throw std::runtime_error(std::string("IW3 FX range is not an object: ") + field);
    return {FxInteger<std::int32_t>(value.at("base"), field),
            FxInteger<std::int32_t>(value.at("amplitude"), field)};
}

PreparedFxVec3Range FxVec3Range(const Json &value, const char *field)
{
    if (!value.is_object())
        throw std::runtime_error(std::string("IW3 FX vector range is not an object: ") + field);
    return {FxFloatArray<3>(value.at("base"), field),
            FxFloatArray<3>(value.at("amplitude"), field)};
}

template <std::size_t Count>
std::array<PreparedFxFloatRange, Count> FxFloatRangeArray(const Json &value,
                                                         const char *field)
{
    if (!value.is_array() || value.size() != Count)
        throw std::runtime_error(std::string("IW3 FX range array has the wrong size: ") + field);
    std::array<PreparedFxFloatRange, Count> result{};
    for (std::size_t index = 0; index < Count; ++index)
        result[index] = FxFloatRange(value.at(index), field);
    return result;
}

PreparedFxVisualState FxVisualState(const Json &value)
{
    if (!value.is_object())
        throw std::runtime_error("IW3 FX visual state is not an object");
    const auto &colors = value.at("color");
    if (!colors.is_array() || colors.size() != 4)
        throw std::runtime_error("IW3 FX visual-state color has the wrong size");
    PreparedFxVisualState result;
    for (std::size_t index = 0; index < result.color.size(); ++index)
        result.color[index] = FxInteger<std::uint8_t>(colors.at(index), "visual color");
    result.rotationDelta = FxFloat(value.at("rotation_delta"), "visual rotation delta");
    result.rotationTotal = FxFloat(value.at("rotation_total"), "visual rotation total");
    result.size = FxFloatArray<2>(value.at("size"), "visual size");
    result.scale = FxFloat(value.at("scale"), "visual scale");
    return result;
}

PreparedFxVelocityFrame FxVelocityFrame(const Json &value)
{
    if (!value.is_object())
        throw std::runtime_error("IW3 FX velocity frame is not an object");
    return {FxVec3Range(value.at("velocity"), "velocity"),
            FxVec3Range(value.at("total_delta"), "velocity total delta")};
}

std::string FxEffectReference(const Json &value, const char *field,
                              std::set<std::pair<std::string, std::string>> &references)
{
    if (value.is_null())
        return {};
    if (!value.is_string())
        throw std::runtime_error(std::string("IW3 FX reference is not a string: ") + field);
    const auto name = value.get<std::string>();
    if (!IsValidFxAssetName(name))
        throw std::runtime_error(std::string("IW3 FX reference has an invalid name: ") + field);
    references.emplace("fx", name);
    return name;
}

PreparedFxVisual FxVisual(const Json &value, const std::uint8_t elementType,
                          std::set<std::pair<std::string, std::string>> &references)
{
    PreparedFxVisual result;
    if (elementType == 9)
    {
        if (!value.is_array() || value.size() != 2)
            throw std::runtime_error("IW3 FX decal visual must contain two material slots");
        result.kind = PreparedFxVisualKind::decal;
        for (const auto &slot : value)
        {
            if (slot.is_null())
            {
                result.names.emplace_back();
                continue;
            }
            if (!slot.is_string() || !IsValidFxAssetName(slot.get<std::string>()))
                throw std::runtime_error("IW3 FX decal has an invalid material name");
            result.names.push_back(slot.get<std::string>());
            references.emplace("material", result.names.back());
        }
        return result;
    }

    if (!value.is_object())
        throw std::runtime_error("IW3 FX visual is not an object");
    const auto type = value.at("type").get<std::string>();
    std::string expected;
    std::string dependencyType;
    if (elementType <= 4)
    {
        result.kind = PreparedFxVisualKind::material;
        expected = dependencyType = "material";
    }
    else if (elementType == 5)
    {
        result.kind = PreparedFxVisualKind::xmodel;
        expected = dependencyType = "xmodel";
    }
    else if (elementType == 8)
    {
        result.kind = PreparedFxVisualKind::sound;
        expected = dependencyType = "sound";
    }
    else if (elementType == 10)
    {
        result.kind = PreparedFxVisualKind::effect;
        expected = dependencyType = "fx";
    }
    else
    {
        result.kind = PreparedFxVisualKind::none;
        expected = "none";
    }
    if (type != expected)
        throw std::runtime_error("IW3 FX visual type does not match its element type");
    if (result.kind == PreparedFxVisualKind::none)
        return result;

    const auto &nameValue = value.at("name");
    if (nameValue.is_null() && result.kind == PreparedFxVisualKind::sound)
    {
        result.names.emplace_back();
        return result;
    }
    if (!nameValue.is_string() || !IsValidFxAssetName(nameValue.get<std::string>()))
        throw std::runtime_error("IW3 FX visual has an invalid asset name");
    result.names.push_back(nameValue.get<std::string>());
    references.emplace(dependencyType, result.names.front());
    return result;
}

std::optional<PreparedFxTrail> FxTrail(const Json &value)
{
    if (value.is_null())
        return std::nullopt;
    if (!value.is_object())
        throw std::runtime_error("IW3 FX trail is not an object");
    PreparedFxTrail result;
    result.scrollTimeMsec = FxInteger<std::int32_t>(value.at("scroll_time_msec"), "trail scroll");
    result.repeatDist = FxInteger<std::int32_t>(value.at("repeat_dist"), "trail repeat distance");
    result.splitDist = FxInteger<std::int32_t>(value.at("split_dist"), "trail split distance");
    const auto &vertices = value.at("vertices");
    const auto &indices = value.at("indices");
    if (!vertices.is_array() || vertices.size() > 16384 || !indices.is_array() ||
        indices.size() > 16384)
        throw std::runtime_error("IW3 FX trail arrays exceed the source limits");
    result.vertices.reserve(vertices.size());
    for (const auto &vertex : vertices)
    {
        if (!vertex.is_object())
            throw std::runtime_error("IW3 FX trail vertex is not an object");
        result.vertices.push_back({FxFloatArray<2>(vertex.at("position"), "trail position"),
                                   FxFloatArray<2>(vertex.at("normal"), "trail normal"),
                                   FxFloat(vertex.at("tex_coord"), "trail texcoord")});
    }
    result.indices.reserve(indices.size());
    for (const auto &index : indices)
    {
        const auto parsed = FxInteger<std::uint16_t>(index, "trail index");
        if (parsed >= result.vertices.size())
            throw std::runtime_error("IW3 FX trail index is outside the vertex array");
        result.indices.push_back(parsed);
    }
    return result;
}

PreparedFxElement FxElement(const Json &value,
                            std::set<std::pair<std::string, std::string>> &references)
{
    if (!value.is_object())
        throw std::runtime_error("IW3 FX element is not an object");
    PreparedFxElement result;
    result.flags = FxInteger<std::int32_t>(value.at("flags"), "element flags");
    const auto &spawn = value.at("spawn");
    if (!spawn.is_object())
        throw std::runtime_error("IW3 FX spawn definition is not an object");
    const auto &raw = spawn.at("raw");
    if (!raw.is_array() || raw.size() != 2)
        throw std::runtime_error("IW3 FX raw spawn union has the wrong size");
    result.spawn.raw = {FxInteger<std::int32_t>(raw.at(0), "spawn raw interval"),
                        FxInteger<std::int32_t>(raw.at(1), "spawn raw count")};
    const auto &looping = spawn.at("looping");
    const auto &oneShot = spawn.at("one_shot");
    if (!looping.is_object() || !oneShot.is_object())
        throw std::runtime_error("IW3 FX spawn views are not objects");
    result.spawn.loopingIntervalMsec =
        FxInteger<std::int32_t>(looping.at("interval_msec"), "spawn interval");
    result.spawn.loopingCount = FxInteger<std::int32_t>(looping.at("count"), "spawn count");
    result.spawn.oneShotCount = FxIntRange(oneShot.at("count"), "one-shot count");
    if (result.spawn.raw[0] != result.spawn.loopingIntervalMsec ||
        result.spawn.raw[1] != result.spawn.loopingCount ||
        result.spawn.raw[0] != result.spawn.oneShotCount.base ||
        result.spawn.raw[1] != result.spawn.oneShotCount.amplitude)
        throw std::runtime_error("IW3 FX spawn union views disagree");

    result.spawnRange = FxFloatRange(value.at("spawn_range"), "spawn range");
    result.fadeInRange = FxFloatRange(value.at("fade_in_range"), "fade-in range");
    result.fadeOutRange = FxFloatRange(value.at("fade_out_range"), "fade-out range");
    result.spawnFrustumCullRadius =
        FxFloat(value.at("spawn_frustum_cull_radius"), "spawn frustum radius");
    result.spawnDelayMsec = FxIntRange(value.at("spawn_delay_msec"), "spawn delay");
    result.lifeSpanMsec = FxIntRange(value.at("life_span_msec"), "life span");
    result.spawnOrigin = FxFloatRangeArray<3>(value.at("spawn_origin"), "spawn origin");
    result.spawnOffsetRadius =
        FxFloatRange(value.at("spawn_offset_radius"), "spawn offset radius");
    result.spawnOffsetHeight =
        FxFloatRange(value.at("spawn_offset_height"), "spawn offset height");
    result.spawnAngles = FxFloatRangeArray<3>(value.at("spawn_angles"), "spawn angles");
    result.angularVelocity =
        FxFloatRangeArray<3>(value.at("angular_velocity"), "angular velocity");
    result.initialRotation = FxFloatRange(value.at("initial_rotation"), "initial rotation");
    result.gravity = FxFloatRange(value.at("gravity"), "gravity");
    result.reflectionFactor = FxFloatRange(value.at("reflection_factor"), "reflection factor");

    const auto &atlas = value.at("atlas");
    if (!atlas.is_object())
        throw std::runtime_error("IW3 FX atlas is not an object");
    result.atlas = {FxInteger<std::uint8_t>(atlas.at("behavior"), "atlas behavior"),
                    FxInteger<std::uint8_t>(atlas.at("index"), "atlas index"),
                    FxInteger<std::uint8_t>(atlas.at("fps"), "atlas fps"),
                    FxInteger<std::uint8_t>(atlas.at("loop_count"), "atlas loop count"),
                    FxInteger<std::uint8_t>(atlas.at("col_index_bits"), "atlas column bits"),
                    FxInteger<std::uint8_t>(atlas.at("row_index_bits"), "atlas row bits"),
                    FxInteger<std::int16_t>(atlas.at("entry_count"), "atlas entry count")};

    result.type = FxInteger<std::uint8_t>(value.at("elem_type"), "element type");
    static constexpr std::array<const char *, 11> typeNames{
        "sprite_billboard", "sprite_oriented", "tail",       "trail", "cloud", "model",
        "omni_light",       "spot_light",      "sound",      "decal", "runner"};
    if (result.type >= typeNames.size() || value.at("elem_type_name").get<std::string>() !=
                                               typeNames[result.type])
        throw std::runtime_error("IW3 FX element type and name disagree");
    result.visualCount = FxInteger<std::uint8_t>(value.at("visual_count"), "visual count");
    result.velocityIntervalCount =
        FxInteger<std::uint8_t>(value.at("vel_interval_count"), "velocity interval count");
    result.visualStateIntervalCount =
        FxInteger<std::uint8_t>(value.at("vis_state_interval_count"),
                                "visual-state interval count");

    const auto &velocitySamples = value.at("velocity_samples");
    if (!velocitySamples.is_array() || velocitySamples.size() > 256 ||
        (!velocitySamples.empty() &&
         velocitySamples.size() != static_cast<std::size_t>(result.velocityIntervalCount) + 1) ||
        (velocitySamples.empty() && result.velocityIntervalCount != 0))
        throw std::runtime_error("IW3 FX velocity sample count is inconsistent");
    result.velocitySamples.reserve(velocitySamples.size());
    for (const auto &sample : velocitySamples)
    {
        if (!sample.is_object())
            throw std::runtime_error("IW3 FX velocity sample is not an object");
        result.velocitySamples.push_back(
            {FxVelocityFrame(sample.at("local")), FxVelocityFrame(sample.at("world"))});
    }

    const auto &visualSamples = value.at("visual_samples");
    if (!visualSamples.is_array() || visualSamples.size() > 256 ||
        (!visualSamples.empty() &&
         visualSamples.size() != static_cast<std::size_t>(result.visualStateIntervalCount) + 1) ||
        (visualSamples.empty() && result.visualStateIntervalCount != 0))
        throw std::runtime_error("IW3 FX visual sample count is inconsistent");
    result.visualSamples.reserve(visualSamples.size());
    for (const auto &sample : visualSamples)
    {
        if (!sample.is_object())
            throw std::runtime_error("IW3 FX visual sample is not an object");
        result.visualSamples.push_back(
            {FxVisualState(sample.at("base")), FxVisualState(sample.at("amplitude"))});
    }

    if (!value.at("visuals_present").is_boolean())
        throw std::runtime_error("IW3 FX visual presence flag is not boolean");
    result.visualsPresent = value.at("visuals_present").get<bool>();
    const auto &visuals = value.at("visuals");
    if (!visuals.is_array() || visuals.size() != result.visualCount ||
        result.visualsPresent != !visuals.empty())
        throw std::runtime_error("IW3 FX visual array does not match its source count");
    result.visuals.reserve(visuals.size());
    for (const auto &visual : visuals)
        result.visuals.push_back(FxVisual(visual, result.type, references));

    result.collisionMins = FxFloatArray<3>(value.at("collision_mins"), "collision minimum");
    result.collisionMaxs = FxFloatArray<3>(value.at("collision_maxs"), "collision maximum");
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (result.collisionMins[axis] > result.collisionMaxs[axis])
            throw std::runtime_error("IW3 FX collision bounds are reversed");
    result.effectOnImpact =
        FxEffectReference(value.at("effect_on_impact"), "effect on impact", references);
    result.effectOnDeath =
        FxEffectReference(value.at("effect_on_death"), "effect on death", references);
    result.effectEmitted =
        FxEffectReference(value.at("effect_emitted"), "emitted effect", references);
    result.emitDist = FxFloatRange(value.at("emit_dist"), "emit distance");
    result.emitDistVariance =
        FxFloatRange(value.at("emit_dist_variance"), "emit distance variance");
    result.trail = FxTrail(value.at("trail"));
    result.sortOrder = FxInteger<std::uint8_t>(value.at("sort_order"), "sort order");
    result.lightingFrac = FxInteger<std::uint8_t>(value.at("lighting_frac"), "lighting fraction");
    result.useItemClip = FxInteger<std::uint8_t>(value.at("use_item_clip"), "item clip flag");
    return result;
}

std::vector<PreparedFx> ReadPreparedFx(const std::filesystem::path &root)
{
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

        try
        {
            const Json graph = ReadJson(iterator->path());
            if (graph.value("schema", 0) != 1 ||
                graph.value("asset_type", std::string{}) != "iw3_fx")
                throw std::runtime_error("schema or asset type is unsupported");
            const auto &layout = graph.at("layout");
            if (!layout.is_object() || layout.value("FxEffectDef", 0) != 32 ||
                layout.value("FxElemDef", 0) != 252 || layout.value("FxTrailDef", 0) != 28)
                throw std::runtime_error("source ABI layout does not match IW3 multiplayer");

            PreparedFx prepared;
            prepared.name = graph.at("name").get<std::string>();
            if (!IsValidFxAssetName(prepared.name))
                throw std::runtime_error("effect name is invalid");
            prepared.sourcePath = iterator->path();
            prepared.flags = FxInteger<std::int32_t>(graph.at("flags"), "effect flags");
            prepared.totalSize = FxInteger<std::int32_t>(graph.at("total_size"), "effect size");
            prepared.msecLoopingLife =
                FxInteger<std::int32_t>(graph.at("msec_looping_life"), "effect looping life");

            const auto &counts = graph.at("element_counts");
            if (!counts.is_object())
                throw std::runtime_error("element counts are not an object");
            prepared.elementCounts = {
                FxInteger<std::uint32_t>(counts.at("looping"), "looping element count"),
                FxInteger<std::uint32_t>(counts.at("one_shot"), "one-shot element count"),
                FxInteger<std::uint32_t>(counts.at("emission"), "emission element count")};
            const auto total = FxInteger<std::uint32_t>(counts.at("total"), "total element count");
            const std::uint64_t computedTotal = static_cast<std::uint64_t>(prepared.elementCounts[0]) +
                                                prepared.elementCounts[1] +
                                                prepared.elementCounts[2];
            const auto &elements = graph.at("elements");
            if (computedTotal != total || total > 4096 || !elements.is_array() ||
                elements.size() != total)
                throw std::runtime_error("element counts do not match the element array");

            std::set<std::pair<std::string, std::string>> declaredDependencies;
            const auto &dependencies = graph.at("dependencies");
            if (!dependencies.is_array() || dependencies.size() > 4096)
                throw std::runtime_error("dependency list is invalid");
            for (const auto &dependency : dependencies)
            {
                if (!dependency.is_object())
                    throw std::runtime_error("dependency is not an object");
                const auto dependencyName = dependency.at("name").get<std::string>();
                const auto dependencyType = dependency.at("type").get<std::string>();
                if (!IsValidFxAssetName(dependencyName) ||
                    (dependencyType != "fx" && dependencyType != "material" &&
                     dependencyType != "image" && dependencyType != "xmodel" &&
                     dependencyType != "sound"))
                    throw std::runtime_error("dependency type or name is unsupported");
                if (!declaredDependencies.emplace(dependencyType, dependencyName).second)
                    throw std::runtime_error("dependency is duplicated");
                prepared.dependencies.push_back({dependencyName, dependencyType});
            }

            std::set<std::pair<std::string, std::string>> elementReferences;
            prepared.elements.reserve(elements.size());
            for (const auto &element : elements)
                prepared.elements.push_back(FxElement(element, elementReferences));
            for (const auto &reference : elementReferences)
                if (!declaredDependencies.contains(reference))
                    throw std::runtime_error("element reference is absent from the dependency list: " +
                                             reference.first + " " + reference.second);
            effects.push_back(std::move(prepared));
        }
        catch (const std::exception &error)
        {
            throw std::runtime_error("invalid IW3 FX source export '" + iterator->path().string() +
                                     "': " + error.what());
        }
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

std::vector<PreparedRawFile> ReadPreparedRawFiles(const std::filesystem::path &root,
                                                  const std::string &sourceMap)
{
    const auto zoneRoot = root / "zone_source";
    std::error_code error;
    std::set<std::string> names;
    for (std::filesystem::directory_iterator iterator(zoneRoot, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file(error) || iterator->path().extension() != ".zone")
            continue;
        const std::string stem = iterator->path().stem().string();
        if (stem != sourceMap && !stem.starts_with(sourceMap + "_"))
            continue;

        std::ifstream input(iterator->path(), std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot read IW3 rawfile zone declarations");
        std::string line;
        while (std::getline(input, line))
        {
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
            const auto comma = line.find(',');
            if (comma == std::string::npos ||
                TrimSourceAssetField(std::string_view(line).substr(0, comma)) != "rawfile")
                continue;
            const auto name = TrimSourceAssetField(
                std::string_view(line).substr(line.rfind(',') + 1));
            const std::filesystem::path relative(name);
            if (name.empty() || relative.has_root_path() ||
                std::find(relative.begin(), relative.end(), std::filesystem::path("..")) !=
                    relative.end())
                throw std::runtime_error("invalid IW3 rawfile declaration name: " + name);
            names.insert(name);
        }
        if (input.bad())
            throw std::runtime_error("cannot read IW3 rawfile zone declarations");
    }
    if (error)
        throw std::runtime_error("cannot enumerate IW3 rawfile zone declarations");

    std::vector<PreparedRawFile> rawFiles;
    rawFiles.reserve(names.size());
    for (const auto &name : names)
    {
        PreparedRawFile rawFile;
        rawFile.name = name;
        if (!zt::read_file((root / std::filesystem::path(name)).string(), rawFile.data))
            throw std::runtime_error("map-local IW3 rawfile payload is missing: " + name);
        if (rawFile.data.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("map-local IW3 rawfile payload is too large: " + name);
        rawFiles.push_back(std::move(rawFile));
    }
    zt::info("iw3: prepared %zu map-local rawfile asset(s)", rawFiles.size());
    return rawFiles;
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
    constexpr std::array<std::string_view, 9> consumedTypes{
        "clipmap", "comworld", "gameworldmp", "mapents",
        "gfxworld", "image", "impactfx", "material", "xmodel"};
    return std::find(consumedTypes.begin(), consumedTypes.end(), type) != consumedTypes.end();
}

std::string_view SourceAssetAuditReason(const std::string_view type)
{
    if (type == "techniqueset")
        return "source techniquesets are not portable; reachable materials use matched Replay "
               "material and techniqueset contracts";
    if (type == "fx")
        return "these source FX graphs use element or material families that are not yet mapped "
               "to native Replay emitters";
    if (type == "sound" || type == "soundcurve" || type == "loadedsound")
        return "audio declarations are outside the native map package";
    if (type == "physpreset")
        return "native collision is built from the clipmap export rather than IW3 presets";
    if (type == "lightdef")
        return "only map-local light_point_linear has a matched Replay LightDef conversion";
    if (type == "gameworldsp")
        return "build-iw3 accepts multiplayer map roots only";
    if (type == "rawfile")
        return "map-local raw files are imported; shared-zone declarations remain stock dependencies";
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

void AuditSourceAssetDeclarations(const std::filesystem::path &root,
                                  const std::set<std::string> &emittedFx,
                                  const std::set<std::string> &emittedRawFiles,
                                  const bool emittedLinearLightDef)
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
        std::set<std::string> unconsumed(names);
        if (type == "fx")
            for (const auto &name : emittedFx)
                unconsumed.erase(name);
        if (type == "rawfile")
            for (const auto &name : emittedRawFiles)
                unconsumed.erase(name);
        if (type == "lightdef" && emittedLinearLightDef)
            unconsumed.erase("light_point_linear");
        if (unconsumed.empty())
            continue;
        const auto examples = SourceAssetExamples(unconsumed);
        zt::warn("iw3: source type '%s' has %zu declaration(s) outside the native map package "
                 "(%s); examples: %s",
                 type.c_str(), unconsumed.size(), SourceAssetAuditReason(type).data(),
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
    result.nativeTopology = !cells.at(0).at("trees").empty() &&
                            cells.at(0).at("trees").at(0).contains("childCount");
    std::vector<std::vector<std::uint32_t>> surfaceTrees;
    if (result.nativeTopology)
        surfaceTrees.resize(surfaces.size());
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
        for (std::size_t treeIndex = 0; treeIndex < trees.size(); ++treeIndex)
        {
            const auto &tree = trees.at(treeIndex);
            ++result.sourceTreeCount;
            const auto &treeSurfaces = tree.at("surfaces");
            const auto &treeModels = tree.at("models");
            if (!treeSurfaces.is_array() || !treeModels.is_array())
                throw std::runtime_error("invalid IW3 DPVS AABB ownership");
            if (!result.nativeTopology && treeSurfaces.empty() && treeModels.empty())
                continue;
            if (result.treeBounds.size() >= VisibilityGroups::Unassigned)
                throw std::runtime_error("IW3 DPVS has too many AABB trees");
            Json bounds = tree.at("bounds");
            if (!bounds.is_array() || bounds.size() != 2)
                throw std::runtime_error("invalid IW3 DPVS AABB bounds");
            const Vec3 minimum = ReadVector<3>(bounds.at(0));
            const Vec3 maximum = ReadVector<3>(bounds.at(1));
            for (std::size_t axis = 0; axis < minimum.size(); ++axis)
                if (minimum[axis] > maximum[axis])
                {
                    if (!result.nativeTopology || !treeSurfaces.empty() ||
                        !treeModels.empty() || tree.at("childCount").get<unsigned>())
                        throw std::runtime_error("reversed IW3 DPVS AABB bounds");
                    bounds = cell.at("bounds");
                    break;
                }

            const auto group = static_cast<std::uint32_t>(result.treeBounds.size());
            result.treeBounds.push_back(bounds);
            if (result.nativeTopology)
            {
                const auto childCount = tree.at("childCount").get<unsigned>();
                const auto firstChild = tree.at("firstChild").get<unsigned>();
                if (childCount > UINT16_MAX ||
                    (childCount && (firstChild <= treeIndex ||
                                    firstChild + childCount > trees.size())) ||
                    (!childCount && firstChild))
                    throw std::runtime_error("invalid IW3 DPVS AABB child range");
                result.treeChildCounts.push_back(static_cast<std::uint16_t>(childCount));
                result.treeFirstChildren.push_back(static_cast<std::uint32_t>(firstChild));
            }
            std::vector<std::uint32_t> ownedModels;
            ownedModels.reserve(treeModels.size());
            for (const auto &model : treeModels)
            {
                const auto index = model.get<std::size_t>();
                assign(result.models, index, group);
                ownedModels.push_back(static_cast<std::uint32_t>(index));
            }
            result.treeModels.push_back(std::move(ownedModels));
            result.cellTrees[cellIndex].push_back(group);
            for (const auto &surface : treeSurfaces)
            {
                const auto index = surface.get<std::size_t>();
                if (result.nativeTopology)
                {
                    if (index >= surfaceTrees.size())
                        throw std::runtime_error("IW3 DPVS surface index is outside its array");
                    surfaceTrees[index].push_back(group);
                }
                else
                    assign(result.surfaces, index, group);
            }
        }
        if (result.nativeTopology && !trees.empty())
        {
            std::vector<bool> visited(trees.size());
            std::vector<std::size_t> pending{0};
            while (!pending.empty())
            {
                const auto index = pending.back();
                pending.pop_back();
                if (visited.at(index))
                    throw std::runtime_error("IW3 DPVS AABB tree has repeated descendants");
                visited[index] = true;
                const auto &tree = trees.at(index);
                const auto first = tree.at("firstChild").get<std::size_t>();
                const auto count = tree.at("childCount").get<std::size_t>();
                for (std::size_t child = first; child < first + count; ++child)
                    pending.push_back(child);
            }
            if (std::ranges::find(visited, false) != visited.end())
                throw std::runtime_error("IW3 DPVS AABB tree has unreachable nodes");
        }
    }
    if (result.nativeTopology)
    {
        std::map<std::vector<std::uint32_t>, std::uint32_t> membershipGroups;
        for (std::size_t index = 0; index < surfaceTrees.size(); ++index)
        {
            auto &members = surfaceTrees[index];
            if (members.empty())
                continue;
            std::ranges::sort(members);
            members.erase(std::unique(members.begin(), members.end()), members.end());
            const auto [it, inserted] = membershipGroups.try_emplace(
                members, static_cast<std::uint32_t>(result.groupTrees.size()));
            if (inserted)
                result.groupTrees.push_back(members);
            result.surfaces[index] = it->second;
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
    // The quaternion stores a proper rotation. A mirrored tangent frame has
    // determinant -1 and cannot be encoded as a quaternion; Replay carries
    // its handedness separately in packed bit 29.
    const Vec3 bitangent = Cross(normal, tangent);
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

unsigned MaterialIndex(const RenderPlan &plan, const MaterialPlan &material,
                       const bool nativeWorld)
{
    if (nativeWorld)
    {
        if (!material.worldMaterialIndex ||
            material.worldMaterialIndex > plan.additionalMaterials.size())
            throw std::runtime_error("IW3 render plan is missing a native world material");
        return material.worldMaterialIndex;
    }
    if (material.kind == SurfaceKind::opaque)
        return 0;
    const std::string suffix = material.kind == SurfaceKind::cutout  ? "_foliage"
                               : material.kind == SurfaceKind::glass ? "_glass"
                                                                     : "_sky";
    for (std::size_t index = 0; index < plan.additionalMaterials.size(); ++index)
        if (plan.additionalMaterials[index].at("material").get<std::string>().ends_with(suffix))
            return static_cast<unsigned>(index + 1);
    throw std::runtime_error("IW3 render plan is missing a material variant");
}

Vec2 EncodedLightmap(const Vertex &vertex, const Surface &surface, const MaterialPlan &material,
                     const RenderPlan &plan, const bool nativeWorld)
{
    float x = 0, y = 0;
    if (surface.lightmap >= 0 && static_cast<std::size_t>(surface.lightmap) >= plan.lightmaps.size())
        throw std::runtime_error("IW3 surface references an invalid lightmap");
    const bool baked = surface.lightmap >= 0;
    if (baked)
    {
        const auto &rectangle = plan.lightmaps[surface.lightmap];
        // A single native lightmap retains its source dimensions and has no
        // offset in the generated material atlas. Replay applies its own atlas
        // placement to these local UVs in the native world pixel shader.
        if (nativeWorld && plan.lightmaps.size() == 1)
            return {std::clamp(vertex.lightmapUv[0], 0.5f / rectangle.width,
                               1.0f - 0.5f / rectangle.width),
                    std::clamp(vertex.lightmapUv[1], 0.5f / rectangle.height,
                               1.0f - 0.5f / rectangle.height)};
        x = (rectangle.x +
             std::clamp(vertex.lightmapUv[0] * rectangle.width, 0.5f, rectangle.width - 0.5f)) /
            4096.0f;
        y = (rectangle.y +
             std::clamp(vertex.lightmapUv[1] * rectangle.height, 0.5f, rectangle.height - 0.5f)) /
            4096.0f;
    }
    if (nativeWorld)
    {
        if (!baked)
            throw std::runtime_error("native Replay world material requires a baked lightmap");
        return {x, y};
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

std::array<Vec3, 3> StaticModelAxis(const SourceStaticModelInstance &instance)
{
    std::array<Vec3, 3> axis{};
    for (std::size_t index = 0; index < axis.size(); ++index)
        axis[index] = Unit(instance.axis[index]);
    if (std::abs(Dot(axis[0], axis[1])) > 0.01f || std::abs(Dot(axis[0], axis[2])) > 0.01f ||
        std::abs(Dot(axis[1], axis[2])) > 0.01f ||
        Dot(Cross(axis[0], axis[1]), axis[2]) < 0.99f)
        throw std::runtime_error("IW3 static-model instance has an invalid rotation basis");
    return axis;
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
        const auto axis = StaticModelAxis(sourceInstance);
        staticModels.instances.push_back(
            {static_cast<unsigned>(sourceInstance.model), sourceInstance.origin,
             QuaternionFromBasis(axis[0], axis[1], axis[2]), sourceInstance.scale});
    }
}

void BuildShadowScene(const std::vector<BrushModel> &brushModels,
                      const SourceStaticModels &staticModels, const RenderPlan &plan,
                      replaysunshadow::Scene &scene)
{
    const auto append = [&](const Surface &source, const unsigned material,
                            const SourceStaticModelInstance *instance,
                            const std::array<Vec3, 3> *axis) {
        if (source.indices.size() % 3 || source.vertices.empty())
            throw std::runtime_error("invalid IW3 shadow-caster surface");
        if (source.indices.empty())
            return;
        replaysunshadow::Surface caster;
        caster.material = material;
        caster.vertices.reserve(source.indices.size());
        // IW3 BSP indices already have the winding expected by Replay's sun
        // rasterizer. Reversing them culls sun-facing single-sided geometry.
        for (std::size_t first = 0; first < source.indices.size(); first += 3)
            for (const std::size_t corner : {0u, 1u, 2u})
            {
                const std::uint32_t index = source.indices[first + corner];
                if (index >= source.vertices.size())
                    throw std::runtime_error("IW3 shadow triangle references a missing vertex");
                const Vertex &vertex = source.vertices[index];
                replaysunshadow::Vertex output;
                output.position = instance
                                      ? Add(instance->origin,
                                            Multiply(Transform(*axis, vertex.position),
                                                     instance->scale))
                                      : vertex.position;
                output.uv = vertex.uv;
                output.alpha = vertex.color[3] / 255.0f;
                for (const float coordinate : output.position)
                    if (!std::isfinite(coordinate) || std::abs(coordinate) > 1.0e7f)
                        throw std::runtime_error("IW3 shadow vertex is outside world range");
                for (const float coordinate : output.uv)
                    if (!std::isfinite(coordinate) || std::abs(coordinate) > 1.0e6f)
                        throw std::runtime_error("IW3 shadow UV is outside supported range");
                caster.vertices.push_back(output);
            }
        scene.surfaces.push_back(std::move(caster));
    };

    if (brushModels.empty())
        throw std::runtime_error("IW3 shadow scene has no fixed world brush model");
    for (const Surface &surface : brushModels.front().surfaces)
    {
        const auto found = plan.worldShadowMaterials.find(surface.material);
        if (found != plan.worldShadowMaterials.end())
            append(surface, found->second, nullptr, nullptr);
    }
    const std::size_t worldSurfaceCount = scene.surfaces.size();
    for (const SourceStaticModelInstance &instance : staticModels.instances)
    {
        if (instance.model >= staticModels.models.size())
            throw std::runtime_error("IW3 shadow instance references a missing model");
        const SourceStaticModel &model = staticModels.models[instance.model];
        if (model.lods.empty())
            throw std::runtime_error("IW3 shadow instance model has no render LOD");
        const auto axis = StaticModelAxis(instance);
        for (const Surface &surface : model.lods.front().surfaces)
        {
            const auto found = plan.modelShadowMaterials.find(surface.material);
            if (found == plan.modelShadowMaterials.end())
                continue;
            if (model.name == "com_ladder_wood")
            {
                Surface corrected = surface;
                AddLadderMidpointRungs(corrected, 0);
                append(corrected, found->second, &instance, &axis);
            }
            else
            {
                append(surface, found->second, &instance, &axis);
            }
        }
    }
    const std::size_t triangles = std::accumulate(
        scene.surfaces.begin(), scene.surfaces.end(), std::size_t{},
        [](const std::size_t count, const replaysunshadow::Surface &surface) {
            return count + surface.vertices.size() / 3;
        });
    zt::info("iw3: prepared sun-shadow scene: %zu material(s), %zu fixed-world and %zu "
             "static-model surface(s), %zu triangle(s)",
             scene.materials.size(), worldSurfaceCount,
             scene.surfaces.size() - worldSurfaceCount, triangles);
}

struct GlassFaceUv
{
    std::string material;
    std::array<float, 4> gradient{};
    Vec2 atSample{};
};

std::optional<GlassFaceUv> BestGlassFaceUv(const BrushModel &model, const RenderPlan &plan,
                                         const std::array<std::size_t, 2> plane,
                                         const Vec3 &sample, const bool intactOnly,
                                         const std::string &wantedMaterial = {})
{
    std::optional<GlassFaceUv> best;
    float largestFace = 0;
    for (const Surface &surface : model.surfaces)
    {
        const auto material = plan.materials.find(surface.material);
        if (material == plan.materials.end() || material->second.glassMaterial.empty() ||
            (intactOnly && surface.material.find("shattered") != std::string::npos) ||
            (!wantedMaterial.empty() && material->second.glassMaterial != wantedMaterial))
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
            GlassFaceUv candidate;
            candidate.material = material->second.glassMaterial;
            for (std::size_t channel = 0; channel < 2; ++channel)
            {
                const float buv = b.uv[channel] - a.uv[channel];
                const float cuv = c.uv[channel] - a.uv[channel];
                const float dx = (buv * cy - cuv * by) / determinant;
                const float dy = (cuv * bx - buv * cx) / determinant;
                candidate.gradient[channel * 2] = dx;
                candidate.gradient[channel * 2 + 1] = dy;
                candidate.atSample[channel] = a.uv[channel] +
                    dx * (sample[plane[0]] - a.position[plane[0]]) +
                    dy * (sample[plane[1]] - a.position[plane[1]]);
            }
            best = std::move(candidate);
        }
    }
    return best;
}

Json BuildGlassPanes(const std::vector<BrushModel> &models, RenderPlan &plan,
                     const std::filesystem::path &mapDirectory, const std::string &map,
                     const std::string &entityText, CollisionData &collision)
{
    Json panes = Json::array();
    const std::vector<EntityFields> entities = ParseEntityFields(entityText);
    for (const EntityFields &entity : entities)
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

        const auto intactFace = BestGlassFaceUv(model, plan, plane, center, true);
        if (!intactFace)
            throw std::runtime_error("IW3 glass pane has no nondegenerate textured face");
        const std::string paneMaterial = intactFace->material;
        std::array<float, 4> texVecs{};
        for (std::size_t index = 0; index < texVecs.size(); ++index)
            texVecs[index] = intactFace->gradient[index] / 32.0f;
        const Vec2 texOrigin = intactFace->atSample;

        // IW3 scripted glass commonly links its intact brush to the shattered
        // replacement by target/targetname. Follow that authored link rather
        // than assuming adjacent brush-model indices or a material name suffix.
        std::string shatteredMaterial = paneMaterial;
        const EntityFields *shatteredEntity = nullptr;
        std::size_t shatteredModel = 0;
        if (const auto target = entity.find("target"); target != entity.end())
        {
            const EntityFields *replacement = nullptr;
            bool unique = true;
            for (const EntityFields &candidate : entities)
                if (const auto name = candidate.find("targetname");
                    name != candidate.end() && name->second == target->second)
                {
                    if (replacement)
                        unique = false;
                    replacement = &candidate;
                }
            if (unique && replacement && replacement->contains("classname") &&
                replacement->at("classname") == "script_brushmodel")
            {
                const auto linkedModel = replacement->find("model");
                if (linkedModel != replacement->end() && linkedModel->second.size() > 1 &&
                    linkedModel->second.front() == '*')
                {
                    std::uint32_t linkedIndex{};
                    const char *begin = linkedModel->second.data() + 1;
                    const char *end = linkedModel->second.data() + linkedModel->second.size();
                    const auto number = std::from_chars(begin, end, linkedIndex);
                    if (number.ec == std::errc{} && number.ptr == end &&
                        linkedIndex < models.size())
                    {
                        std::string linkedMaterial;
                        bool ambiguous = false;
                        for (const Surface &surface : models[linkedIndex].surfaces)
                        {
                            const auto material = plan.materials.find(surface.material);
                            if (material == plan.materials.end() ||
                                material->second.kind != SurfaceKind::glass ||
                                material->second.glassMaterial.empty())
                                continue;
                            if (!linkedMaterial.empty() &&
                                linkedMaterial != material->second.glassMaterial)
                                ambiguous = true;
                            linkedMaterial = material->second.glassMaterial;
                        }
                        if (!ambiguous && !linkedMaterial.empty())
                        {
                            shatteredMaterial = linkedMaterial;
                            shatteredEntity = replacement;
                            shatteredModel = linkedIndex;
                        }
                    }
                }
            }
        }

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
        if (shatteredEntity && shatteredMaterial != paneMaterial)
        {
            const auto linkedAxis = EntityAxis(*shatteredEntity);
            Vec3 linkedOrigin{};
            if (const auto found = shatteredEntity->find("origin");
                found != shatteredEntity->end())
                linkedOrigin = ParseVector(found->second);
            const Vec3 relative = Subtract(origin, linkedOrigin);
            const Vec3 linkedSample{Dot(relative, linkedAxis[0]), Dot(relative, linkedAxis[1]),
                                    Dot(relative, linkedAxis[2])};
            const BrushModel &linkedModel = models.at(shatteredModel);
            Vec3 linkedSize{};
            for (std::size_t dimension = 0; dimension < 3; ++dimension)
                linkedSize[dimension] = linkedModel.maximum[dimension] -
                                        linkedModel.minimum[dimension];
            const std::size_t linkedThin = static_cast<std::size_t>(
                std::min_element(linkedSize.begin(), linkedSize.end()) - linkedSize.begin());
            std::array<std::size_t, 2> linkedPlane{};
            std::size_t linkedPlaneIndex = 0;
            for (std::size_t dimension = 0; dimension < 3; ++dimension)
                if (dimension != linkedThin)
                    linkedPlane[linkedPlaneIndex++] = dimension;
            const auto linkedFace = BestGlassFaceUv(linkedModel, plan, linkedPlane,
                                                     linkedSample, false, shatteredMaterial);
            if (!linkedFace)
                throw std::runtime_error("linked IW3 shattered glass has no textured face");
            const auto paneGradient = intactFace->gradient;
            const float determinant = paneGradient[0] * paneGradient[3] -
                                      paneGradient[1] * paneGradient[2];
            if (std::abs(determinant) < 1.0e-10f)
                throw std::runtime_error("IW3 glass pane UV basis cannot be inverted");
            const auto linkedGradient = [&](const std::size_t channel,
                                            const Vec3 &direction) {
                return linkedFace->gradient[channel * 2] *
                           Dot(direction, linkedAxis[linkedPlane[0]]) +
                       linkedFace->gradient[channel * 2 + 1] *
                           Dot(direction, linkedAxis[linkedPlane[1]]);
            };
            const float su = linkedGradient(0, worldU), sv = linkedGradient(0, worldV);
            const float tu = linkedGradient(1, worldU), tv = linkedGradient(1, worldV);
            const float inv = 1.0f / determinant;
            std::array<float, 6> remap{
                (su * paneGradient[3] - sv * paneGradient[2]) * inv,
                (sv * paneGradient[0] - su * paneGradient[1]) * inv,
                (tu * paneGradient[3] - tv * paneGradient[2]) * inv,
                (tv * paneGradient[0] - tu * paneGradient[1]) * inv, 0, 0};
            remap[4] = linkedFace->atSample[0] - remap[0] * texOrigin[0] -
                       remap[1] * texOrigin[1];
            remap[5] = linkedFace->atSample[1] - remap[2] * texOrigin[0] -
                       remap[3] * texOrigin[1];
            const std::array<float, 6> identity{1, 0, 0, 1, 0, 0};
            bool differs = false;
            for (std::size_t index = 0; index < remap.size(); ++index)
            {
                if (!std::isfinite(remap[index]))
                    throw std::runtime_error("linked IW3 shattered glass has invalid UV mapping");
                differs |= std::abs(remap[index] - identity[index]) > 0.00001f;
            }
            if (differs)
                shatteredMaterial = RegisterGlassUvRemap(plan, mapDirectory, map,
                                                         shatteredMaterial, remap);
        }
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
                         {"materialShattered", shatteredMaterial},
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
                 const std::vector<BrushModel> &models, RenderPlan &plan,
                 const std::filesystem::path &mapDirectory, const std::string &map,
                 const std::string &entities, CollisionData &collision,
                 std::size_t &triangleCount)
{
    const unsigned sunCount = world.at("sun_primary_light_index").get<unsigned>();
    if (sunCount > 5)
        throw std::runtime_error("IW3 sun-light count exceeds Replay surface-shadow mask");
    const unsigned sunShadowMask = ((1u << sunCount) - 1u) << 1;
    Json output = {{"schema", 1},
                   {"material", plan.material},
                   {"materialDefinition", plan.materialDefinition},
                   {"additionalMaterials", plan.additionalMaterials},
                   {"assetMaterials", plan.assetMaterials},
                   {"reflectionProbeArrayImage", plan.reflectionProbeArrayImage},
                   {"nativeLightmaps", Json::array()},
                   {"atlasVertexLayout", 3},
                   {"brushModels", Json::array()},
                   {"glassPanes", Json::array()},
                   {"reflectionProbes", Json::array()},
                   {"surfaces", Json::array()}};
    for (const auto &lightmap : plan.nativeLightmaps)
    {
        Json images = Json::array();
        for (std::size_t channel = 0; channel < lightmap.files.size(); ++channel)
            images.push_back({{"format", lightmap.formats[channel]},
                              {"pixels", lightmap.files[channel]}});
        output["nativeLightmaps"].push_back(
            {{"width", lightmap.width}, {"height", lightmap.height}, {"images", images}});
    }
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
    // Geometry and cell lists still use source indices, including the reserved
    // default probe. The render plan contains only selectable captures.
    const auto sourceProbeCount = world.at("reflection_probes").size();
    std::vector<ProbeBounds> geometryProbeBounds(sourceProbeCount);
    std::vector<ProbeBounds> cellProbeBounds(sourceProbeCount);

    const auto appendSky = [&] {
        MaterialPlan skyPlan;
        skyPlan.kind = SurfaceKind::sky;
        const unsigned skyMaterial = MaterialIndex(plan, skyPlan, false);
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
            const bool nativeWorld = material.kind == SurfaceKind::opaque && surface.lightmap >= 0;
            const unsigned materialIndex = MaterialIndex(plan, material, nativeWorld);
            std::ostringstream key;
            key << materialIndex;
            for (const float value : material.environment)
                key << ':' << value;
            key << ':' << surface.visibilityGroup;
            key << ':' << surface.reflectionProbe;
            // Several IW3 materials share one Replay fallback material. Keep
            // their caster eligibility separate when merging fixed and entity brushes.
            key << ':' << material.castsShadow;
            if (!target || activeKey != key.str() ||
                targetVertices + surface.vertices.size() > 60000 ||
                targetIndices + surface.indices.size() > 65535u * 3u)
            {
                output["surfaces"].push_back({{"vertices", Json::array()},
                                              {"indices", Json::array()},
                                              {"materialIndex", materialIndex},
                                              {"atlasVertexLayout", nativeWorld ? 1u : 3u},
                                              {"lightmapIndex", 0u},
                                              {"opaque", material.kind == SurfaceKind::opaque},
                                              {"sunShadowMask", material.castsShadow ? sunShadowMask : 0u},
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
                    if (visibility.nativeTopology)
                    {
                        for (const auto tree : visibility.groupTrees.at(surface.visibilityGroup))
                            treeSurfaces.at(tree).insert(destinationSurface);
                    }
                    else
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
                        const Vec2 lightmap =
                            EncodedLightmap(vertex, surface, material, plan, nativeWorld);
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
        for (std::size_t treeIndex = 0; treeIndex < visibility.cellTrees.at(cellIndex).size();
             ++treeIndex)
        {
            const auto group = visibility.cellTrees.at(cellIndex).at(treeIndex);
            const auto &owned = treeSurfaces.at(group);
            const auto &ownedModels = visibility.treeModels.at(group);
            if (visibility.nativeTopology || !owned.empty() || !ownedModels.empty())
            {
                Json tree = {{"bounds", visibility.treeBounds.at(group)},
                             {"surfaces", std::vector<unsigned>(owned.begin(), owned.end())},
                             {"models", std::vector<unsigned>(ownedModels.begin(),
                                                                ownedModels.end())}};
                if (visibility.nativeTopology)
                {
                    tree["childCount"] = visibility.treeChildCounts.at(group);
                    tree["firstChild"] = visibility.treeFirstChildren.at(group);
                    if (!treeIndex && !globalSurfaces.empty())
                    {
                        auto all = tree.at("surfaces").get<std::vector<unsigned>>();
                        all.insert(all.end(), globalSurfaces.begin(), globalSurfaces.end());
                        std::ranges::sort(all);
                        all.erase(std::unique(all.begin(), all.end()), all.end());
                        tree["surfaces"] = std::move(all);
                        tree["bounds"] = world.at("bounds");
                    }
                }
                trees.push_back(std::move(tree));
            }
        }
        if (!visibility.nativeTopology && !globalSurfaces.empty())
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
        const auto sourceIndex = probe.sourceIndex;
        if (sourceIndex >= sourceProbeCount)
            throw std::runtime_error("IW3 render plan references an invalid reflection probe");
        Vec3 minimum{}, maximum{};
        if (sourceIndex == 0)
        {
            minimum = ReadVector<3>(worldBounds.at(0));
            maximum = ReadVector<3>(worldBounds.at(1));
        }
        else if (geometryProbeBounds[sourceIndex].valid)
        {
            minimum = geometryProbeBounds[sourceIndex].minimum;
            maximum = geometryProbeBounds[sourceIndex].maximum;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] -= 32.0f;
                maximum[axis] += 32.0f;
            }
        }
        else if (cellProbeBounds[sourceIndex].valid)
        {
            minimum = cellProbeBounds[sourceIndex].minimum;
            maximum = cellProbeBounds[sourceIndex].maximum;
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
    output["glassPanes"] = BuildGlassPanes(models, plan, mapDirectory, map,
                                          entities, collision);
    output["assetMaterials"] = plan.assetMaterials;
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

void AlignLadderEdgesToModels(CollisionData &collision, const SourceStaticModels &models)
{
    struct LadderModel
    {
        std::string name;
        std::uint32_t material{};
        Vec3 minimum{INFINITY, INFINITY, INFINITY};
        Vec3 maximum{-INFINITY, -INFINITY, -INFINITY};
        std::vector<Vec3> vertices;
        float rungPhase{};
        bool hasRungPhase{};
    };
    std::vector<LadderModel> ladders;
    for (const SourceStaticModelInstance &instance : models.instances)
    {
        const SourceStaticModel &model = models.models.at(instance.model);
        std::string name = model.name;
        std::ranges::transform(name, name.begin(), [](const unsigned char letter) {
            return static_cast<char>(std::tolower(letter));
        });
        if (name.find("ladder") == std::string::npos || model.lods.empty())
            continue;

        LadderModel candidate;
        candidate.name = model.name;
        if (!model.lods.front().surfaces.empty())
        {
            candidate.material = FootstepMaterial(model.lods.front().surfaces.front().material);
            for (const Surface &surface : model.lods.front().surfaces)
                if (FootstepMaterial(surface.material) != candidate.material)
                {
                    candidate.material = 0;
                    break;
                }
        }
        double phaseCos = 0.0, phaseSin = 0.0;
        std::size_t rungTriangles = 0;
        for (const Surface &surface : model.lods.front().surfaces)
        {
            std::vector<Vec3> worldVertices;
            worldVertices.reserve(surface.vertices.size());
            for (const Vertex &vertex : surface.vertices)
            {
                const Vec3 position = Add(
                    instance.origin,
                    Multiply(Transform(instance.axis, vertex.position), instance.scale));
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    candidate.minimum[axis] = (std::min)(candidate.minimum[axis], position[axis]);
                    candidate.maximum[axis] = (std::max)(candidate.maximum[axis], position[axis]);
                }
                candidate.vertices.push_back(position);
                worldVertices.push_back(position);
            }
            for (std::size_t first = 0; first + 2 < surface.indices.size(); first += 3)
            {
                const Vec3 &a = worldVertices.at(surface.indices[first]);
                const Vec3 &b = worldVertices.at(surface.indices[first + 1]);
                const Vec3 &c = worldVertices.at(surface.indices[first + 2]);
                const float low = (std::min)({a[2], b[2], c[2]});
                const float high = (std::max)({a[2], b[2], c[2]});
                const auto broad = [](const Vec3 &p, const Vec3 &q) {
                    const float dx = p[0] - q[0], dy = p[1] - q[1];
                    return dx * dx + dy * dy;
                };
                if (high - low > 4.0f ||
                    (std::max)({broad(a, b), broad(b, c), broad(c, a)}) < 144.0f)
                    continue;
                const double angle = (double(a[2]) + b[2] + c[2]) * (2.0 * 3.141592653589793 / 36.0);
                phaseCos += std::cos(angle);
                phaseSin += std::sin(angle);
                ++rungTriangles;
            }
        }
        if (rungTriangles >= 16 &&
            std::hypot(phaseCos, phaseSin) / rungTriangles >= 0.75)
        {
            candidate.rungPhase = static_cast<float>(
                std::atan2(phaseSin, phaseCos) * (12.0 / (2.0 * 3.141592653589793)));
            const float halfUnit = std::round(candidate.rungPhase * 2.0f) * 0.5f;
            if (std::abs(candidate.rungPhase - halfUnit) < 0.15f)
                candidate.rungPhase = halfUnit;
            candidate.hasRungPhase = true;
        }
        if (!candidate.vertices.empty())
            ladders.push_back(std::move(candidate));
    }

    std::size_t aligned = 0;
    for (CollisionHull &hull : collision.hulls)
    {
        if (hull.ladderPlanes.empty() || hull.points.empty())
            continue;
        Vec3 minimum{INFINITY, INFINITY, INFINITY};
        Vec3 maximum{-INFINITY, -INFINITY, -INFINITY};
        for (const Vec3 &point : hull.points)
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = (std::min)(minimum[axis], point[axis]);
                maximum[axis] = (std::max)(maximum[axis], point[axis]);
            }
        const Vec3 center{(minimum[0] + maximum[0]) * 0.5f,
                          (minimum[1] + maximum[1]) * 0.5f,
                          (minimum[2] + maximum[2]) * 0.5f};

        const LadderModel *best = nullptr;
        float bestScore = INFINITY;
        for (const LadderModel &model : ladders)
        {
            const float overlapZ = (std::min)(maximum[2], model.maximum[2]) -
                                   (std::max)(minimum[2], model.minimum[2]);
            if (overlapZ < 12.0f)
                continue;
            const float modelX = (model.minimum[0] + model.maximum[0]) * 0.5f;
            const float modelY = (model.minimum[1] + model.maximum[1]) * 0.5f;
            const float modelZ = (model.minimum[2] + model.maximum[2]) * 0.5f;
            const float dx = modelX - center[0], dy = modelY - center[1];
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared > 24.0f * 24.0f)
                continue;
            const float dz = modelZ - center[2];
            const float score = distanceSquared + dz * dz * 0.02f;
            if (score < bestScore)
            {
                bestScore = score;
                best = &model;
            }
        }
        if (!best)
            continue;

        // The IW3 brush marks the climbable volume, which can be wider and
        // deeper than its rendered ladder. Replay IK uses the edge centerline
        // as its hand anchor. Keep the collision hull and normal unchanged;
        // move only the serialized edge plane to the visible model surface.
        std::vector<float> adjusted;
        std::vector<std::array<float, 2>> modelTangents;
        adjusted.reserve(hull.ladderPlanes.size());
        modelTangents.reserve(hull.ladderPlanes.size());
        bool valid = true;
        for (const Vec4 &plane : hull.ladderPlanes)
        {
            float modelFace = -INFINITY;
            float modelTangentMin = INFINITY;
            float modelTangentMax = -INFINITY;
            float hullTangentMin = INFINITY;
            float hullTangentMax = -INFINITY;
            const float tangentX = -plane[1], tangentY = plane[0];
            for (const Vec3 &position : best->vertices)
            {
                modelFace = (std::max)(modelFace, plane[0] * position[0] +
                                                     plane[1] * position[1] +
                                                     plane[2] * position[2]);
                const float tangent = tangentX * position[0] + tangentY * position[1];
                modelTangentMin = (std::min)(modelTangentMin, tangent);
                modelTangentMax = (std::max)(modelTangentMax, tangent);
            }
            for (const Vec3 &position : hull.points)
            {
                const float tangent = tangentX * position[0] + tangentY * position[1];
                hullTangentMin = (std::min)(hullTangentMin, tangent);
                hullTangentMax = (std::max)(hullTangentMax, tangent);
            }
            const float inset = plane[3] - modelFace;
            const float width = modelTangentMax - modelTangentMin;
            if (!std::isfinite(modelFace) || inset < -2.0f || inset > 16.0f ||
                width < 12.0f || modelTangentMin < hullTangentMin - 2.0f ||
                modelTangentMax > hullTangentMax + 2.0f)
            {
                valid = false;
                break;
            }
            adjusted.push_back(modelFace);
            modelTangents.push_back({(modelTangentMin + modelTangentMax) * 0.5f, width});
        }
        if (!valid)
            continue;
        for (std::size_t index = 0; index < adjusted.size(); ++index)
            hull.ladderPlanes[index][3] = adjusted[index];
        hull.ladderModelTangents = std::move(modelTangents);
        hull.materialOverride = best->material;
        if (best->hasRungPhase)
            hull.ladderRungOffset =
                std::remainder(best->rungPhase + 2.0f - minimum[2], 12.0f);
        ++aligned;
        zt::info("iw3: aligned ladder edge to %s at (%.1f %.1f %.1f), rung phase offset %.1f",
                 best->name.c_str(), center[0], center[1], center[2], hull.ladderRungOffset);
    }
    if (aligned)
        zt::info("iw3: aligned %zu ladder edge pairs to rendered models", aligned);
}

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
    std::vector<std::uint8_t> output{'M', 'W', 'C', 'O', 'L', 'L', '1', '2'};
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
        append(hull.materialOverride);
        append(hull.ladderRungOffset);
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
        for (std::size_t index = 0; index < hull.ladderPlanes.size(); ++index)
        {
            const std::array<float, 2> tangent = hull.ladderModelTangents.empty()
                                                     ? std::array<float, 2>{}
                                                     : hull.ladderModelTangents.at(index);
            append(tangent[0]);
            append(tangent[1]);
        }
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

template <typename Payload>
iw8::vfx::Module VfxValueModule(const iw8_focus::ParticleModuleType type,
                                const Payload &payload)
{
    static_assert(sizeof(Payload) <= 0xE0);
    iw8::vfx::Module module;
    module.native.moduleType = static_cast<std::uint16_t>(type);
    std::memcpy(module.native.moduleData, &payload, sizeof(payload));
    return module;
}

iw8_focus::ParticleFloatRange VfxRange(const PreparedFxFloatRange &source,
                                       const float scale = 1.0f)
{
    const float first = source.base * scale;
    const float second = (source.base + source.amplitude) * scale;
    return {std::min(first, second), std::max(first, second)};
}

iw8_focus::ParticleIntRange VfxRange(const PreparedFxIntRange &source)
{
    const std::int32_t second = source.base + source.amplitude;
    return {std::min(source.base, second), std::max(source.base, second)};
}

iw8::vfx::Module::Curve VfxCurve(const std::vector<float> &values, const float scale)
{
    iw8::vfx::Module::Curve curve;
    curve.points.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const float time = values.size() <= 1
                               ? 0.0f
                               : static_cast<float>(index) /
                                     static_cast<float>(values.size() - 1);
        const float previous = index == 0
                                   ? 0.0f
                                   : static_cast<float>(index - 1) /
                                         static_cast<float>(values.size() - 1);
        curve.points.push_back(
            {time, scale == 0.0f ? values[index] : values[index] / scale,
             index == 0 ? 0.0f : 1.0f / (time - previous), 0});
    }
    return curve;
}

float VfxCurveScale(const std::vector<float> &first, const std::vector<float> &second,
                    const float fallback = 1.0f)
{
    float scale = 0.0f;
    for (const float value : first)
        scale = std::max(scale, std::abs(value));
    for (const float value : second)
        scale = std::max(scale, std::abs(value));
    return scale == 0.0f ? fallback : scale;
}

std::string VfxMaterial(const PreparedMap &map, const std::string &name)
{
    const auto material = map.fxMaterialAliases.find(name);
    if (material == map.fxMaterialAliases.end())
        throw std::runtime_error("IW3 VFX material has no converted Replay asset: " + name);
    return material->second;
}

void VfxSetVec(iw8_focus::vec4_t &target, const float x, const float y, const float z,
               const float w = 0.0f)
{
    target.v[0] = x;
    target.v[1] = y;
    target.v[2] = z;
    target.v[3] = w;
}

iw8::vfx::Module VfxSpawnModule()
{
    iw8_focus::ParticleModuleInitSpawn payload{};
    payload.base.type = static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initSpawn);
    payload.curve.scale = 1.0f;
    auto module = VfxValueModule(iw8_focus::ParticleModuleType::initSpawn, payload);
    module.spawnCurve = {{0.0f, 1.0f, 0.0f, 0}, {1.0f, 1.0f, 1.0f, 0}};
    return module;
}

iw8::vfx::Module VfxAttributesModule()
{
    iw8_focus::ParticleModuleInitAttributes payload{};
    payload.base.type =
        static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initAttributes);
    VfxSetVec(payload.sizeMin, 1.0f, 1.0f, 1.0f);
    VfxSetVec(payload.sizeMax, 1.0f, 1.0f, 1.0f);
    VfxSetVec(payload.colorMin, 1.0f, 1.0f, 1.0f, 1.0f);
    VfxSetVec(payload.colorMax, 1.0f, 1.0f, 1.0f, 1.0f);
    return VfxValueModule(iw8_focus::ParticleModuleType::initAttributes, payload);
}

void AddVfxSpawnShape(const PreparedFxElement &source, iw8::vfx::State &state)
{
    const auto radius = VfxRange(source.spawnOffsetRadius);
    const auto height = VfxRange(source.spawnOffsetHeight);
    const bool hasOffset = radius.min != 0.0f || radius.max != 0.0f ||
                           height.min != 0.0f || height.max != 0.0f ||
                           std::ranges::any_of(source.spawnOrigin, [](const auto &range) {
                               return range.base != 0.0f || range.amplitude != 0.0f;
                           });
    if (!hasOffset && source.type != 6)
        return;

    if (source.type == 6)
    {
        iw8_focus::ParticleModuleInitSpawnShapeBox payload{};
        payload.base.base.type = static_cast<std::uint16_t>(
            iw8_focus::ParticleModuleType::initSpawnShapeBox);
        payload.base.axisFlags = 0x3F;
        payload.base.normalAxis = 2;
        payload.base.spawnType = 0;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float minimum = source.spawnOrigin[axis].base;
            const float maximum = minimum + source.spawnOrigin[axis].amplitude;
            payload.dimensionsMin.v[axis] = std::min(minimum, maximum);
            payload.dimensionsMax.v[axis] = std::max(minimum, maximum);
            payload.base.offset.v[axis] = (minimum + maximum) * 0.5f;
        }
        auto module = VfxValueModule(iw8_focus::ParticleModuleType::initSpawnShapeBox,
                                     payload);
        module.curves.resize(6);
        for (auto &curve : module.curves)
            curve = VfxCurve({1.0f, 1.0f}, 0.0f);
        state.groups[0].push_back(std::move(module));
        return;
    }

    iw8_focus::ParticleModuleInitSpawnShapeCylinder payload{};
    payload.base.base.type = static_cast<std::uint16_t>(
        iw8_focus::ParticleModuleType::initSpawnShapeCylinder);
    payload.base.axisFlags = 0x3F;
    payload.base.normalAxis = 0;
    payload.base.spawnType = 1;
    for (std::size_t axis = 0; axis < 3; ++axis)
        payload.base.offset.v[axis] =
            source.spawnOrigin[axis].base + source.spawnOrigin[axis].amplitude * 0.5f;
    payload.halfHeight = std::max(std::abs(height.min), std::abs(height.max));
    payload.radius = radius;
    const float volume = 6.28318530718f * radius.max * radius.max * payload.halfHeight;
    payload.base.volumeCubeRoot = volume > 0.0f ? std::cbrt(volume) : 0.0f;
    auto module =
        VfxValueModule(iw8_focus::ParticleModuleType::initSpawnShapeCylinder, payload);
    module.curves.resize(5);
    for (auto &curve : module.curves)
        curve = VfxCurve({1.0f, 1.0f}, 0.0f);
    state.groups[0].push_back(std::move(module));
}

void AddVfxVelocity(const PreparedFxElement &source, iw8::vfx::State &state)
{
    if (source.velocitySamples.empty())
        return;
    constexpr std::int32_t runMask = 0x1C0;
    const bool world = (source.flags & runMask) == 0;
    std::array<std::vector<float>, 6> values;
    for (const auto &sample : source.velocitySamples)
    {
        const auto &velocity = world ? sample.world.velocity : sample.local.velocity;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            values[axis].push_back(velocity.base[axis] * 1000.0f);
            values[axis + 3].push_back(
                (velocity.base[axis] + velocity.amplitude[axis]) * 1000.0f);
        }
    }
    if (std::ranges::all_of(values, [](const auto &curve) {
            return std::ranges::all_of(curve,
                                       [](const float value) { return value == 0.0f; });
        }))
        return;

    iw8_focus::ParticleModuleInitRelativeVelocity relative{};
    relative.base.type = static_cast<std::uint16_t>(
        iw8_focus::ParticleModuleType::initRelativeVelocity);
    relative.velocityType = world ? 1u : 2u;
    state.groups[0].push_back(
        VfxValueModule(iw8_focus::ParticleModuleType::initRelativeVelocity, relative));

    iw8_focus::ParticleModuleVelocityGraph payload{};
    payload.base.type =
        static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::velocityGraph);
    if (world)
        payload.base.flags |= 0x80;
    auto module = VfxValueModule(iw8_focus::ParticleModuleType::velocityGraph, payload);
    module.curves.resize(6);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const float scale = VfxCurveScale(values[axis], values[axis + 3]);
        module.curves[axis] = VfxCurve(values[axis], scale);
        module.curves[axis + 3] = VfxCurve(values[axis + 3], scale);
        iw8_focus::ParticleModuleVelocityGraph stored{};
        std::memcpy(&stored, module.native.moduleData, sizeof(payload));
        stored.curves[axis].scale = scale;
        stored.curves[axis + 3].scale = scale;
        if (values[axis] != values[axis + 3])
            stored.base.flags |= 0x10;
        std::memcpy(module.native.moduleData, &stored, sizeof(stored));
    }
    state.groups[1].push_back(std::move(module));
}

void AddVfxRotation(const PreparedFxElement &source, const bool model,
                    iw8::vfx::State &state)
{
    if (model)
    {
        iw8_focus::ParticleModuleInitRotation3D payload{};
        payload.base.type =
            static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initRotation3D);
        const auto angle = VfxRange(source.initialRotation);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            payload.rotationAngleMin.v[axis] = angle.min;
            payload.rotationAngleMax.v[axis] = angle.max;
            const auto rate = VfxRange(source.angularVelocity[axis], 1000.0f);
            payload.rotationRateMin.v[axis] = rate.min;
            payload.rotationRateMax.v[axis] = rate.max;
        }
        state.groups[0].push_back(
            VfxValueModule(iw8_focus::ParticleModuleType::initRotation3D, payload));
        return;
    }
    if (source.initialRotation.base == 0.0f && source.initialRotation.amplitude == 0.0f)
        return;
    iw8_focus::ParticleModuleInitRotation payload{};
    payload.base.type =
        static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initRotation);
    payload.base.flags = source.initialRotation.amplitude == 0.0f ? 0u : 1u;
    payload.rotationAngle = VfxRange(source.initialRotation);
    state.groups[0].push_back(
        VfxValueModule(iw8_focus::ParticleModuleType::initRotation, payload));
}

void AddVfxVisualCurves(const PreparedFxElement &source, iw8::vfx::State &state)
{
    if (source.visualSamples.empty())
        return;
    std::array<std::vector<float>, 8> colors;
    std::array<std::vector<float>, 6> sizes;
    for (const auto &sample : source.visualSamples)
    {
        constexpr std::array<std::size_t, 4> sourceColorChannels{2, 1, 0, 3};
        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            const auto sourceChannel = sourceColorChannels[channel];
            colors[channel].push_back(
                static_cast<float>(sample.base.color[sourceChannel]) / 255.0f);
            colors[channel + 4].push_back(
                static_cast<float>(sample.amplitude.color[sourceChannel]) / 255.0f);
        }
        const float baseScale = sample.base.scale;
        const float maxScale = baseScale + sample.amplitude.scale;
        std::array<float, 3> base{sample.base.size[0], sample.base.size[1], baseScale};
        std::array<float, 3> maximum{
            sample.base.size[0] + sample.amplitude.size[0],
            sample.base.size[1] + sample.amplitude.size[1], maxScale};
        if (source.type == 5)
        {
            base = {baseScale, baseScale, baseScale};
            maximum = {maxScale, maxScale, maxScale};
        }
        else if (source.type == 6)
        {
            // IW3 omni lights animate radius in size[0]. Its unused second
            // channel maps to Replay's neutral brightness multiplier.
            if (base[1] == 0.0f && maximum[1] == 0.0f)
                base[1] = maximum[1] = 1.0f;
            base[2] = maximum[2] = 1.0f;
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            sizes[axis].push_back(base[axis]);
            sizes[axis + 3].push_back(maximum[axis]);
        }
    }

    iw8_focus::ParticleModuleColorGraph colorPayload{};
    colorPayload.base.type =
        static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::colorGraph);
    colorPayload.firstCurve = 1;
    auto color = VfxValueModule(iw8_focus::ParticleModuleType::colorGraph, colorPayload);
    color.curves.resize(8);
    for (std::size_t index = 0; index < colors.size(); ++index)
        color.curves[index] = VfxCurve(colors[index], 1.0f);
    colorPayload.base.flags =
        std::equal(colors.begin(), colors.begin() + 4, colors.begin() + 4) ? 0u : 0x10u;
    std::memcpy(color.native.moduleData, &colorPayload, sizeof(colorPayload));
    state.groups[1].push_back(std::move(color));

    iw8_focus::ParticleModuleSizeGraph sizePayload{};
    sizePayload.base.type =
        static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::sizeGraph);
    sizePayload.firstCurve = 1;
    auto size = VfxValueModule(iw8_focus::ParticleModuleType::sizeGraph, sizePayload);
    size.curves.resize(6);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const float scale = VfxCurveScale(sizes[axis], sizes[axis + 3]);
        size.curves[axis] = VfxCurve(sizes[axis], scale);
        size.curves[axis + 3] = VfxCurve(sizes[axis + 3], scale);
        sizePayload.curves[axis].scale = scale;
        sizePayload.curves[axis + 3].scale = scale;
        if (sizes[axis] != sizes[axis + 3])
            sizePayload.base.flags |= 0x10;
    }
    std::memcpy(size.native.moduleData, &sizePayload, sizeof(sizePayload));
    state.groups[1].push_back(std::move(size));
}

void AddVfxGravity(const PreparedFxElement &source, iw8::vfx::State &state)
{
    if (source.gravity.base == 0.0f && source.gravity.amplitude == 0.0f)
        return;
    iw8_focus::ParticleModuleGravity payload{};
    payload.base.type = static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::gravity);
    payload.percentage = VfxRange(source.gravity);
    state.groups[1].push_back(
        VfxValueModule(iw8_focus::ParticleModuleType::gravity, payload));
}

void AddVfxImpact(
    const PreparedFxElement &source,
    const std::unordered_map<std::string, std::string> &effectAliases,
    iw8::vfx::State &state)
{
    if (source.effectOnImpact.empty())
        return;
    if (source.type != 5)
        throw std::runtime_error("IW3 FX impact conversion is pinned only for model particles");

    iw8_focus::ParticleModulePhysicsRayCast rayCast{};
    rayCast.base.type =
        static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::physicsRayCast);
    rayCast.bounce = VfxRange(source.reflectionFactor);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        rayCast.bounds.midPoint.v[axis] =
            (source.collisionMins[axis] + source.collisionMaxs[axis]) * 0.5f;
        rayCast.bounds.halfSize.v[axis] =
            (source.collisionMaxs[axis] - source.collisionMins[axis]) * 0.5f;
    }
    rayCast.useItemClip = source.useItemClip;
    state.groups[1].push_back(
        VfxValueModule(iw8_focus::ParticleModuleType::physicsRayCast, rayCast));

    const auto child = effectAliases.find(source.effectOnImpact);
    if (child == effectAliases.end())
        throw std::runtime_error("IW3 FX impact child was not converted: " +
                                 source.effectOnImpact);
    iw8_focus::ParticleModuleTestImpact impact{};
    impact.test.base.type =
        static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::testImpact);
    impact.test.moduleIndex = static_cast<std::uint16_t>(state.groups[2].size());
    impact.test.orientationOptions = 6;
    impact.test.eventHandlerData.kill = 1;
    auto module = VfxValueModule(iw8_focus::ParticleModuleType::testImpact, impact);
    module.childEffects.push_back(child->second);
    state.groups[2].push_back(std::move(module));
}

void AddVfxBirth(
    const PreparedFxElement &source,
    const std::unordered_map<std::string, std::string> &effectAliases,
    iw8::vfx::State &state)
{
    if (source.effectEmitted.empty())
        return;
    const auto child = effectAliases.find(source.effectEmitted);
    if (child == effectAliases.end())
        throw std::runtime_error("IW3 FX birth child was not converted: " +
                                 source.effectEmitted);

    iw8_focus::ParticleModuleTest birth{};
    birth.base.type = static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::testBirth);
    birth.moduleIndex = static_cast<std::uint16_t>(state.groups[2].size());
    birth.orientationOptions = 6;
    auto module = VfxValueModule(iw8_focus::ParticleModuleType::testBirth, birth);
    module.childEffects.push_back(child->second);
    state.groups[2].push_back(std::move(module));
    state.native.flags |= 0x100000000ull;
}

void AddVfxAtlas(const PreparedFxElement &source, iw8::vfx::State &state)
{
    if (source.atlas.entryCount <= 1)
        return;
    iw8_focus::ParticleModuleInitAtlas payload{};
    payload.base.type = static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initAtlas);
    payload.startFrame = source.atlas.index;
    payload.loopCount = source.atlas.loopCount;
    payload.randomIndex = source.atlas.behavior == 0 ? 1 : 0;
    payload.playOverLife = source.atlas.behavior == 0 ? 0 : 1;
    payload.curves[0].scale = 1.0f;
    payload.curves[1].scale = 1.0f;
    auto module = VfxValueModule(iw8_focus::ParticleModuleType::initAtlas, payload);
    module.curves = {VfxCurve({0.0f, 1.0f}, 1.0f), VfxCurve({1.0f, 1.0f}, 1.0f)};
    state.groups[0].push_back(std::move(module));
}

iw8::vfx::Emitter ConvertFxElement(
    const PreparedMap &map, const PreparedFxElement &source,
    const std::unordered_map<std::string, std::string> &effectAliases,
    const std::string &lightDef)
{
    iw8::vfx::Emitter emitter;
    const auto burst = VfxRange(source.spawn.oneShotCount);
    const std::uint32_t maxCount = static_cast<std::uint32_t>(std::max(1, burst.max));
    emitter.native.particleSpawnRate = {static_cast<float>(maxCount),
                                        static_cast<float>(maxCount)};
    emitter.native.particleLife = VfxRange(
        {static_cast<float>(source.lifeSpanMsec.base),
         static_cast<float>(source.lifeSpanMsec.amplitude)},
        0.001f);
    emitter.native.particleDelay = VfxRange(
        {static_cast<float>(source.spawnDelayMsec.base),
         static_cast<float>(source.spawnDelayMsec.amplitude)},
        0.001f);
    emitter.native.particleCountMax = maxCount;
    emitter.native.particleBurstCount = {std::max(1, burst.min), std::max(1, burst.max)};
    const auto distance = VfxRange(source.spawnRange);
    emitter.native.spawnRangeSq = {distance.min * distance.min, distance.max * distance.max};
    emitter.native.spawnFrustumCullRadius = source.spawnFrustumCullRadius;
    emitter.native.emitByDistanceDensity = {0.1f, 0.1f};

    iw8::vfx::State state;
    const bool model = source.type == 5;
    switch (source.type)
    {
    case 0:
        state.native.elementType = 0;
        state.native.flags = 550830604356ull;
        emitter.native.flags = 4194306;
        emitter.native.dataFlags = 30603519;
        break;
    case 1:
        state.native.elementType = 8;
        state.native.flags = 550830635076ull;
        emitter.native.flags = 2;
        emitter.native.dataFlags = 26409215;
        break;
    case 2:
        state.native.elementType = 10;
        state.native.flags = 1074790400ull;
        emitter.native.flags = 4194306;
        emitter.native.dataFlags = 26409215;
        break;
    case 4:
        // Replay keeps the legacy initCloud selector in its ABI, but none of
        // the shipped common-zone VFX uses it. Native cloud/smoke emitters use
        // an element-0 material state with this measured flag family.
        state.native.elementType = 0;
        state.native.flags = 550830630912ull;
        emitter.native.flags = 4194306;
        emitter.native.dataFlags = 30603519;
        break;
    case 5:
        state.native.elementType = 7;
        state.native.flags = 549755815044ull;
        emitter.native.flags = 2;
        emitter.native.dataFlags = 131529215;
        break;
    case 6:
        state.native.elementType = 5;
        state.native.flags = 549756862468ull;
        emitter.native.flags = 130;
        emitter.native.dataFlags = 26409215;
        break;
    case 9:
        state.native.elementType = 2;
        state.native.flags = 549755844672ull;
        emitter.native.flags = 2;
        emitter.native.dataFlags = 26409215;
        break;
    case 10:
        state.native.elementType = 9;
        state.native.flags = 4296015872ull;
        emitter.native.flags = 1048578;
        emitter.native.dataFlags = 26410239;
        break;
    default:
        throw std::runtime_error("IW3 FX graph contains an unsupported element type");
    }
    if (!source.effectOnImpact.empty())
    {
        if (!model)
            throw std::runtime_error("IW3 FX impact conversion is pinned only for model particles");
        // Shipped Replay debris with PHYSICS_RAY_CAST + TEST_IMPACT uses this
        // exact model-state family. The collision bit is 0x10000 in m_dataFlags.
        state.native.flags = 4297130116ull;
        emitter.native.flags = 4194306;
        emitter.native.dataFlags = 131594751;
    }

    state.groups[0].push_back(VfxSpawnModule());
    state.groups[0].push_back(VfxAttributesModule());
    if (source.type == 10)
    {
        iw8_focus::ParticleModuleInitRunner payload{};
        payload.base.type =
            static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initRunner);
        VfxSetVec(payload.scaleMin, 1.0f, 1.0f, 1.0f);
        VfxSetVec(payload.scaleMax, 1.0f, 1.0f, 1.0f);
        VfxSetVec(payload.velocityMin, 1.0f, 1.0f, 1.0f);
        VfxSetVec(payload.velocityMax, 1.0f, 1.0f, 1.0f);
        payload.orientationOptions = 2;
        payload.attachToParent = 1;
        payload.stopChildOnDeath = 1;
        payload.killChildOnDeath = 1;
        payload.legacyOrientationRotation = 1;
        auto module = VfxValueModule(iw8_focus::ParticleModuleType::initRunner, payload);
        for (const auto &visual : source.visuals)
        {
            if (visual.kind != PreparedFxVisualKind::effect || visual.names.size() != 1)
                throw std::runtime_error("IW3 runner has an invalid child effect");
            const auto alias = effectAliases.find(visual.names.front());
            if (alias == effectAliases.end())
                throw std::runtime_error("IW3 runner child effect was not converted: " +
                                         visual.names.front());
            module.childEffects.push_back(alias->second);
        }
        state.groups[0].push_back(std::move(module));
        emitter.states.push_back(std::move(state));
        return emitter;
    }
    if (source.type == 6)
    {
        if (lightDef.empty())
            throw std::runtime_error("IW3 omni light has no Replay LightDef");
        if (!std::ranges::all_of(source.visuals, [](const PreparedFxVisual &visual) {
                return visual.kind == PreparedFxVisualKind::none && visual.names.empty();
            }))
            throw std::runtime_error("IW3 omni light has an invalid visual definition");
        iw8_focus::ParticleModuleInitLightOmni payload{};
        payload.base.type =
            static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initLightOmni);
        payload.base.flags = 2048;
        payload.fovOuter = 0.7853981852531433f;
        payload.bulbRadius = 2.0f;
        payload.bulbLength = 1.0f / 255.0f;
        payload.distanceFalloff = 1.0f;
        payload.brightness = 1.0f;
        payload.shadowSoftness = 0.55f;
        payload.shadowBias = 0.4f;
        payload.shadowArea = 0.018f;
        payload.toneMappingScaleFactor = 1.0f;
        payload.disableVolumetric = 1;
        payload.disableDynamicShadows = 1;
        payload.scriptScale = 1;
        auto module = VfxValueModule(iw8_focus::ParticleModuleType::initLightOmni, payload);
        module.lightDefs.push_back(lightDef);
        state.groups[0].push_back(std::move(module));
    }
    else if (source.type == 9)
    {
        iw8_focus::ParticleModuleInitDecal payload{};
        payload.base.type =
            static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initDecal);
        auto module = VfxValueModule(iw8_focus::ParticleModuleType::initDecal, payload);
        for (const auto &visual : source.visuals)
        {
            if (visual.kind != PreparedFxVisualKind::decal || visual.names.size() != 2)
                throw std::runtime_error("IW3 decal has an invalid material pair");
            // Replay consumes one selected platform/profile material in all three
            // ParticleMarkVisuals slots. The second IW3 decal entry is the WC
            // world-context material used by this Windows Replay target.
            const auto material = VfxMaterial(map, visual.names[1]);
            module.decalMaterials.push_back({material, material, material});
        }
        state.groups[0].push_back(std::move(module));
    }
    else
    {
        iw8::vfx::Module module;
        if (model)
        {
            iw8_focus::ParticleModuleInitModel payload{};
            payload.base =
                static_cast<std::uint64_t>(iw8_focus::ParticleModuleType::initModel);
            module = VfxValueModule(iw8_focus::ParticleModuleType::initModel, payload);
            for (const auto &visual : source.visuals)
            {
                if (visual.kind != PreparedFxVisualKind::xmodel || visual.names.size() != 1)
                    throw std::runtime_error("IW3 model particle has an invalid XModel visual");
                module.models.push_back(visual.names.front());
            }
        }
        else
        {
            if (source.type == 1)
            {
                iw8_focus::ParticleModuleInitOrientedSprite oriented{};
                oriented.base.type = static_cast<std::uint16_t>(
                    iw8_focus::ParticleModuleType::initOrientedSprite);
                VfxSetVec(oriented.orientationQuat, 0.0f, 0.0f, 0.0f, 1.0f);
                state.groups[0].push_back(VfxValueModule(
                    iw8_focus::ParticleModuleType::initOrientedSprite, oriented));
            }
            if (source.type == 2)
            {
                iw8_focus::ParticleModuleInitTail tail{};
                tail.base.type =
                    static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initTail);
                state.groups[0].push_back(
                    VfxValueModule(iw8_focus::ParticleModuleType::initTail, tail));
            }
            iw8_focus::ParticleModuleInitMaterial payload{};
            payload.base.type =
                static_cast<std::uint16_t>(iw8_focus::ParticleModuleType::initMaterial);
            module = VfxValueModule(iw8_focus::ParticleModuleType::initMaterial, payload);
            for (const auto &visual : source.visuals)
            {
                if (visual.kind != PreparedFxVisualKind::material || visual.names.size() != 1)
                    throw std::runtime_error("IW3 particle has an invalid material visual");
                module.materials.push_back(VfxMaterial(map, visual.names.front()));
            }
        }
        state.groups[0].push_back(std::move(module));
    }

    AddVfxAtlas(source, state);
    AddVfxSpawnShape(source, state);
    AddVfxVelocity(source, state);
    AddVfxRotation(source, model, state);
    AddVfxGravity(source, state);
    AddVfxImpact(source, effectAliases, state);
    AddVfxBirth(source, effectAliases, state);
    AddVfxVisualCurves(source, state);
    emitter.states.push_back(std::move(state));
    return emitter;
}

iw8::vfx::Effect ConvertFx(
    const PreparedMap &map, const PreparedFx &source,
    const std::unordered_map<std::string, std::string> &effectAliases,
    const std::string &lightDef)
{
    if (source.elements.empty())
        throw std::runtime_error("IW3 FX graph has no elements: " + source.name);
    iw8::vfx::Effect effect;
    effect.name = effectAliases.at(source.name);
    effect.native.flags = 262145;
    if (std::ranges::any_of(source.elements,
                            [](const PreparedFxElement &element) { return element.type == 6; }))
        effect.native.flags |= 0x4;
    effect.native.occlusionOverrideEmitterIndex = -1;
    effect.native.drawFrustumCullRadius = 350.0f;
    effect.native.updateFrustumCullRadius = 500.0f;
    effect.native.sunDistance = 100000.0f;
    effect.emitters.reserve(source.elements.size());
    for (const auto &element : source.elements)
        effect.emitters.push_back(ConvertFxElement(map, element, effectAliases, lightDef));
    return effect;
}

bool HasDirectReplayFxMapping(const PreparedMap &map, const PreparedFx &effect)
{
    return !effect.elements.empty() &&
           std::ranges::all_of(effect.elements, [&](const PreparedFxElement &element) {
               const bool supportedType = element.type == 0 || element.type == 1 ||
                                          element.type == 2 ||
                                          element.type == 4 || element.type == 5 ||
                                          element.type == 6 ||
                                          element.type == 9 || element.type == 10;
               if (!supportedType || !element.effectOnDeath.empty() ||
                   (!element.effectOnImpact.empty() && element.type != 5) ||
                   (!element.effectEmitted.empty() && element.type == 10))
                   return false;
               if (element.type == 0 || element.type == 1 || element.type == 2 ||
                   element.type == 4)
                   return std::ranges::all_of(element.visuals, [&](const PreparedFxVisual &visual) {
                       return visual.kind == PreparedFxVisualKind::material &&
                              visual.names.size() == 1 &&
                              map.fxMaterialAliases.contains(visual.names.front());
                   });
               if (element.type == 6)
                   return std::ranges::all_of(
                       element.visuals, [](const PreparedFxVisual &visual) {
                           return visual.kind == PreparedFxVisualKind::none &&
                                  visual.names.empty();
                       });
               if (element.type == 9)
                   return std::ranges::all_of(
                       element.visuals, [&](const PreparedFxVisual &visual) {
                           // The Windows Replay decal module selects IW3's
                           // world-context (second) material. Do not admit a
                           // graph unless that exact material has a converted
                           // Replay carrier; ConvertFxElement would otherwise
                           // discover the missing asset only after aliasing the
                           // parent graph.
                           return visual.kind == PreparedFxVisualKind::decal &&
                                  visual.names.size() == 2 &&
                                  map.fxMaterialAliases.contains(visual.names[1]);
                       });
               return true;
           });
}

std::unordered_map<std::string, std::string>
BuildFxAliases(const PreparedMap &map, const std::string &targetMap)
{
    const auto &effects = map.fxEffects;
    std::set<std::string> convertible;
    for (const auto &effect : effects)
        if (HasDirectReplayFxMapping(map, effect))
            convertible.insert(effect.name);

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (auto iterator = convertible.begin(); iterator != convertible.end();)
        {
            const auto effect = std::ranges::find(effects, *iterator, &PreparedFx::name);
            const bool missingChild = std::ranges::any_of(
                effect->elements, [&](const PreparedFxElement &element) {
                    const bool missingRunner =
                        element.type == 10 &&
                        std::ranges::any_of(element.visuals, [&](const PreparedFxVisual &visual) {
                            return visual.kind != PreparedFxVisualKind::effect ||
                                   visual.names.size() != 1 ||
                                   !convertible.contains(visual.names.front());
                        });
                    const bool missingImpact = !element.effectOnImpact.empty() &&
                                               !convertible.contains(element.effectOnImpact);
                    const bool missingBirth = !element.effectEmitted.empty() &&
                                              !convertible.contains(element.effectEmitted);
                    return missingRunner || missingImpact || missingBirth;
                });
            if (missingChild)
            {
                iterator = convertible.erase(iterator);
                changed = true;
            }
            else
            {
                ++iterator;
            }
        }
    }

    std::unordered_map<std::string, std::string> aliases;
    aliases.reserve(convertible.size());
    for (const auto &name : convertible)
        aliases.emplace(name, "mw120r/" + targetMap + "/fx/" + name);
    return aliases;
}

iw8::impact::EffectOverrides ReadImpactOverrides(
    const std::filesystem::path &root,
    const std::vector<std::filesystem::path> &sourcePaths,
    const std::unordered_map<std::string, std::string> &effectAliases)
{
    constexpr std::array<std::pair<std::size_t, std::size_t>, 12> targetRows{{
        {1, 0},  // bullet small
        {1, 1},  // bullet small exit
        {2, 0},  // bullet large
        {2, 1},  // bullet large exit
        {6, 0},  // shotgun
        {6, 1},  // shotgun exit
        {3, 0},  // armor piercing
        {3, 1},  // armor piercing exit
        {9, 0},  // grenade bounce
        {10, 0}, // grenade explosion
        {12, 0}, // rocket explosion
        {14, 0}, // projectile dud
    }};
    constexpr std::size_t nonFleshCount = 29;
    constexpr std::size_t fleshCount = 4;

    std::filesystem::path path;
    const auto relative = std::filesystem::path("impactfx") / "_default.iw3.json";
    std::vector<std::filesystem::path> roots{root};
    roots.insert(roots.end(), sourcePaths.begin(), sourcePaths.end());
    std::error_code pathError;
    for (const auto &source : roots)
    {
        for (const auto &candidate : {source / relative, source / "raw" / relative})
        {
            if (std::filesystem::is_regular_file(candidate, pathError))
            {
                path = candidate;
                break;
            }
            pathError.clear();
        }
        if (!path.empty())
            break;
    }
    if (path.empty())
        throw std::runtime_error(
            "IW3 impact FX export is missing; add common_mp.ff or an extracted IW3 raw "
            "directory with --search-path");
    const Json source = ReadJson(path);
    const auto &layout = source.at("layout");
    const auto &entries = source.at("entries");
    if (source.value("schema", 0) != 1 ||
        source.value("asset_type", std::string{}) != "iw3_impact_fx" ||
        !layout.is_object() || layout.value("impact_count", 0) != targetRows.size() ||
        layout.value("nonflesh_count", 0) != nonFleshCount ||
        layout.value("flesh_count", 0) != fleshCount || !entries.is_array() ||
        entries.size() != targetRows.size())
        throw std::runtime_error("IW3 impact FX export has an unsupported schema or layout");

    iw8::impact::EffectOverrides overrides;
    overrides.reserve(targetRows.size() * (nonFleshCount + fleshCount));
    for (std::size_t sourceRow = 0; sourceRow < targetRows.size(); ++sourceRow)
    {
        const auto &entry = entries.at(sourceRow);
        if (entry.at("index").get<std::size_t>() != sourceRow)
            throw std::runtime_error("IW3 impact FX row index is inconsistent");
        const auto &nonFlesh = entry.at("nonflesh");
        const auto &flesh = entry.at("flesh");
        if (!nonFlesh.is_array() || nonFlesh.size() != nonFleshCount ||
            !flesh.is_array() || flesh.size() != fleshCount)
            throw std::runtime_error("IW3 impact FX row has invalid surface arrays");

        const auto append = [&](const Json &value, const bool isFlesh,
                                const std::size_t index) {
            std::string effect;
            if (!value.is_null())
            {
                if (!value.is_string() || !IsValidFxAssetName(value.get<std::string>()))
                    throw std::runtime_error("IW3 impact FX slot has an invalid effect name");
                const auto alias = effectAliases.find(value.get<std::string>());
                if (alias == effectAliases.end())
                    return;
                effect = "," + alias->second;
            }
            overrides.push_back({targetRows[sourceRow].first, targetRows[sourceRow].second,
                                 isFlesh, index, std::move(effect)});
        };
        for (std::size_t index = 0; index < nonFleshCount; ++index)
            append(nonFlesh.at(index), false, index);
        for (std::size_t index = 0; index < fleshCount; ++index)
            append(flesh.at(index), true, index);
    }
    return overrides;
}
} // namespace

PreparedMap::PreparedMap(PreparedMap &&other) noexcept
    : root(std::move(other.root))
    , collision(std::move(other.collision))
    , footsteps(std::move(other.footsteps))
    , scratch(std::move(other.scratch))
    , xmodels(std::move(other.xmodels))
    , rawFiles(std::move(other.rawFiles))
    , fxEffects(std::move(other.fxEffects))
    , fxMaterialAliases(std::move(other.fxMaterialAliases))
    , vfxEffects(std::move(other.vfxEffects))
    , vfxLightDef(std::move(other.vfxLightDef))
    , smallGlassEffect(std::move(other.smallGlassEffect))
    , impactOverrides(std::move(other.impactOverrides))
    , staticModels(std::move(other.staticModels))
    , shadowScene(std::move(other.shadowScene))
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
        rawFiles = std::move(other.rawFiles);
        fxEffects = std::move(other.fxEffects);
        fxMaterialAliases = std::move(other.fxMaterialAliases);
        vfxEffects = std::move(other.vfxEffects);
        vfxLightDef = std::move(other.vfxLightDef);
        smallGlassEffect = std::move(other.smallGlassEffect);
        impactOverrides = std::move(other.impactOverrides);
        staticModels = std::move(other.staticModels);
        shadowScene = std::move(other.shadowScene);
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
        // A direct conversion may be given an already-extracted IW3 asset pack
        // instead of a full installation containing common_mp.ff.  Stage only
        // the entity models the map names, then retain the normal common-zone
        // fallback for ordinary IW3 installations.
        CopyExtractedEntityModels(exportRoot, missingEntityModels, options.searchPaths);
        missingEntityModels = MissingEntityModels(exportRoot, entityModelNames);
    }
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
    CopyExtractedFx(exportRoot, options.searchPaths);
    CopyExtractedFxMaterials(exportRoot, options.searchPaths);
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

    result.rawFiles = ReadPreparedRawFiles(exportRoot, sourceMap);
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
    std::array<std::size_t, 11> fxElementTypes{};
    std::size_t fxElementCount = 0;
    for (const auto &effect : result.fxEffects)
        for (const auto &element : effect.elements)
        {
            ++fxElementTypes.at(element.type);
            ++fxElementCount;
        }
    zt::info("iw3: validated %zu element(s) in %zu map-reachable source FX graph(s) from "
             "%zu declaration(s)",
             fxElementCount, result.fxEffects.size(), declaredFx.size());
    zt::info("iw3: FX elements sprite=%zu oriented=%zu tail=%zu trail=%zu cloud=%zu model=%zu "
             "omni=%zu spot=%zu sound=%zu decal=%zu runner=%zu",
             fxElementTypes[0], fxElementTypes[1], fxElementTypes[2], fxElementTypes[3],
             fxElementTypes[4], fxElementTypes[5], fxElementTypes[6], fxElementTypes[7],
             fxElementTypes[8], fxElementTypes[9], fxElementTypes[10]);
    std::vector<std::string> fxDependencyModels;
    for (const auto &effect : result.fxEffects)
        for (const auto &dependency : effect.dependencies)
            if (dependency.type == "xmodel")
                fxDependencyModels.push_back(dependency.name);
    std::ranges::sort(fxDependencyModels);
    fxDependencyModels.erase(
        std::unique(fxDependencyModels.begin(), fxDependencyModels.end()), fxDependencyModels.end());
    CopyExtractedEntityModels(exportRoot, MissingEntityModels(exportRoot, fxDependencyModels),
                              options.searchPaths);
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
    std::vector<std::string> worldMaterialNames;
    std::vector<std::string> modelMaterialNames;
    std::vector<std::string> shadowWorldMaterialNames;
    std::vector<std::string> shadowModelMaterialNames;
    for (const Surface &surface : brushModels.front().surfaces)
        shadowWorldMaterialNames.push_back(surface.material);
    for (const SourceStaticModel &model : sourceStaticModels.models)
        if (!model.lods.empty())
            for (const Surface &surface : model.lods.front().surfaces)
                shadowModelMaterialNames.push_back(surface.material);
    for (const BrushModel &model : brushModels)
        for (const Surface &surface : model.surfaces)
        {
            materialNames.push_back(surface.material);
            worldMaterialNames.push_back(surface.material);
        }
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
    const std::vector<std::string> fxMaterials(fxMaterialNames.begin(), fxMaterialNames.end());
    RenderPlan renderPlan =
        PrepareRenderAssets(exportRoot, world, materialNames, worldMaterialNames,
                            modelMaterialNames, fxMaterials, shadowWorldMaterialNames,
                            shadowModelMaterialNames, options.searchPaths, mapDirectory,
                            options.map);
    result.fxMaterialAliases = renderPlan.fxMaterialAliases;
    BuildPreparedStaticModels(sourceStaticModels, renderPlan, options.map, "smodel", result.xmodels,
                              result.staticModels, true);
    result.shadowScene = std::move(renderPlan.shadowScene);
    BuildShadowScene(brushModels, sourceStaticModels, renderPlan, result.shadowScene);
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
    const auto effectAliases = BuildFxAliases(result, options.map);
    const bool hasOmniLights = std::ranges::any_of(
        result.fxEffects, [&](const PreparedFx &effect) {
            return effectAliases.contains(effect.name) &&
                   std::ranges::any_of(effect.elements, [](const PreparedFxElement &element) {
                       return element.type == 6;
                   });
        });
    if (hasOmniLights)
        result.vfxLightDef = "mw120r/" + options.map + "/light_fx_default";
    result.vfxEffects.reserve(effectAliases.size());
    std::unordered_map<std::string, const PreparedFx *> convertibleEffects;
    convertibleEffects.reserve(effectAliases.size());
    for (const auto &effect : result.fxEffects)
    {
        if (effectAliases.contains(effect.name))
            convertibleEffects.emplace(effect.name, &effect);
    }
    std::unordered_map<std::string, std::uint8_t> effectStates;
    effectStates.reserve(effectAliases.size());
    const auto appendEffect = [&](auto &&self, const PreparedFx &effect) -> void {
        auto &state = effectStates[effect.name];
        if (state == 2)
            return;
        if (state == 1)
            throw std::runtime_error("IW3 FX graph contains a child dependency cycle: " +
                                     effect.name);
        state = 1;
        for (const auto &element : effect.elements)
        {
            if (element.type == 10)
            {
                for (const auto &visual : element.visuals)
                {
                    const auto child = convertibleEffects.find(visual.names.front());
                    if (child == convertibleEffects.end())
                        throw std::runtime_error("IW3 runner child effect was not converted: " +
                                                 visual.names.front());
                    self(self, *child->second);
                }
            }
            if (!element.effectOnImpact.empty())
            {
                const auto child = convertibleEffects.find(element.effectOnImpact);
                if (child == convertibleEffects.end())
                    throw std::runtime_error("IW3 impact child effect was not converted: " +
                                             element.effectOnImpact);
                self(self, *child->second);
            }
            if (!element.effectEmitted.empty())
            {
                const auto child = convertibleEffects.find(element.effectEmitted);
                if (child == convertibleEffects.end())
                    throw std::runtime_error("IW3 birth child effect was not converted: " +
                                             element.effectEmitted);
                self(self, *child->second);
            }
        }
        result.vfxEffects.push_back(
            ConvertFx(result, effect, effectAliases, result.vfxLightDef));
        if (effect.name == "impacts/small_glass")
            result.smallGlassEffect = effectAliases.at(effect.name);
        state = 2;
    };
    for (const auto &effect : result.fxEffects)
        if (effectAliases.contains(effect.name))
            appendEffect(appendEffect, effect);
    zt::info("iw3: converted %zu of %zu map-reachable source FX graph(s) to native Replay VFX",
             result.vfxEffects.size(), result.fxEffects.size());
    if (!result.smallGlassEffect.empty())
        zt::info("iw3: converted impacts/small_glass to native Replay VFX '%s'",
                 result.smallGlassEffect.c_str());
    result.impactOverrides = ReadImpactOverrides(exportRoot, options.searchPaths, effectAliases);
    zt::info("iw3: mapped %zu native IW3 impact slots into Replay's impact table",
             result.impactOverrides.size());
    std::set<std::string> emittedFx;
    for (const auto &entry : effectAliases)
        emittedFx.insert(entry.first);
    std::set<std::string> emittedRawFiles;
    for (const auto &rawFile : result.rawFiles)
        emittedRawFiles.insert(rawFile.name);
    AuditSourceAssetDeclarations(exportRoot, emittedFx, emittedRawFiles, localLinearLightDef);
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
    AlignLadderEdgesToModels(nativeCollision, sourceStaticModels);
    const Json render = BuildRender(world, visibility, brushModels, renderPlan,
                                    mapDirectory, options.map, entities, nativeCollision,
                                    triangles);
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
