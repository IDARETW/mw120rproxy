#include "iw3_fastfile.h"
#include "iw3_lightgrid.h"
#include "iw3_render_assets.h"

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

struct CollisionHull
{
    std::vector<Vec3> points;
    std::uint32_t contents{1};
    std::uint32_t model{};
};

struct CollisionModel
{
    Vec3 minimum{};
    Vec3 maximum{};
};

struct CollisionData
{
    std::vector<CollisionHull> hulls;
    std::vector<CollisionModel> models;
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

std::filesystem::path MakeScratchDirectory()
{
    wchar_t temporaryRoot[MAX_PATH]{};
    if (!GetTempPathW(std::size(temporaryRoot), temporaryRoot))
    {
        throw std::runtime_error("cannot locate the Windows temporary directory");
    }

    wchar_t temporaryFile[MAX_PATH]{};
    if (!GetTempFileNameW(temporaryRoot, L"iw3", 0, temporaryFile) || !DeleteFileW(temporaryFile) ||
        !CreateDirectoryW(temporaryFile, nullptr))
    {
        throw std::runtime_error("cannot create a private IW3 conversion directory");
    }
    return std::filesystem::path(temporaryFile);
}

void RunUnlinker(const std::filesystem::path &unlinker, const ImportOptions &options,
                 const std::filesystem::path &output)
{
    std::vector<std::wstring> arguments{unlinker.wstring(),
                                        L"--no-color",
                                        L"--image-format",
                                        L"DDS",
                                        L"--model-format",
                                        L"OBJ",
                                        L"-o",
                                        output.wstring()};

    std::vector<std::filesystem::path> searchPaths = options.searchPaths;
    searchPaths.push_back(options.fastfile.parent_path());
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

    arguments.push_back(std::filesystem::absolute(options.fastfile).wstring());
    const auto loadFastfile =
        options.fastfile.parent_path() / (options.fastfile.stem().wstring() + L"_load.ff");
    std::error_code error;
    if (std::filesystem::is_regular_file(loadFastfile, error))
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

    const auto logPath = output / "unlinker.log";
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

    const DWORD wait = WaitForSingleObject(process.hProcess, 10u * 60u * 1000u);
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
        throw std::runtime_error("OpenAssetTools failed while reading the IW3 fastfile");
    }
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
        vertex.tangent = Unit(ReadVector<3>(item.at("tangent")));
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

int ObjIndex(const std::string_view text, const std::size_t count)
{
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
    {
        throw std::runtime_error("invalid OBJ index in IW3 model");
    }
    const int index = value > 0 ? value - 1 : static_cast<int>(count) + value;
    if (index < 0 || static_cast<std::size_t>(index) >= count)
    {
        throw std::runtime_error("OBJ index is outside its source array");
    }
    return index;
}

std::array<std::string_view, 3> SplitObjReference(const std::string &text)
{
    std::array<std::string_view, 3> result{};
    std::string_view value(text);
    std::size_t start = 0;
    for (std::size_t index = 0; index < 3; ++index)
    {
        const std::size_t slash = value.find('/', start);
        result[index] = value.substr(start, slash == std::string_view::npos ? value.size() - start
                                                                            : slash - start);
        if (slash == std::string_view::npos)
        {
            break;
        }
        start = slash + 1;
    }
    return result;
}

std::vector<Surface> ReadObj(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("cannot read IW3 model " + path.string());
    }

    std::vector<Vec3> positions;
    std::vector<Vec2> textureCoordinates;
    std::vector<Vec3> normals;
    std::vector<Surface> surfaces;
    Surface *active = nullptr;
    std::string line;
    while (std::getline(input, line))
    {
        std::istringstream row(line);
        std::string operation;
        row >> operation;
        if (operation == "v")
        {
            Vec3 value{};
            if (!(row >> value[0] >> value[1] >> value[2]))
            {
                throw std::runtime_error("invalid OBJ position in IW3 model");
            }
            positions.push_back(value);
        }
        else if (operation == "vt")
        {
            Vec2 value{};
            if (!(row >> value[0] >> value[1]))
            {
                throw std::runtime_error("invalid OBJ UV in IW3 model");
            }
            textureCoordinates.push_back(value);
        }
        else if (operation == "vn")
        {
            Vec3 value{};
            if (!(row >> value[0] >> value[1] >> value[2]))
            {
                throw std::runtime_error("invalid OBJ normal in IW3 model");
            }
            normals.push_back(value);
        }
        else if (operation == "usemtl")
        {
            std::string material;
            row >> material;
            if (material.empty())
                throw std::runtime_error("IW3 model OBJ has an empty material");
            surfaces.emplace_back();
            surfaces.back().material = std::move(material);
            active = &surfaces.back();
        }
        else if (operation == "f")
        {
            std::vector<std::string> references;
            for (std::string reference; row >> reference;)
            {
                references.push_back(std::move(reference));
            }
            if (references.size() != 3)
            {
                throw std::runtime_error("IW3 model OBJ must contain triangulated faces");
            }
            if (!active)
                throw std::runtime_error("IW3 model OBJ triangle has no material");

            std::array<Vertex, 3> face{};
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto parts = SplitObjReference(references[corner]);
                face[corner].position = positions[ObjIndex(parts[0], positions.size())];
                if (!parts[1].empty())
                {
                    face[corner].uv =
                        textureCoordinates[ObjIndex(parts[1], textureCoordinates.size())];
                    face[corner].uv[1] = 1.0f - face[corner].uv[1];
                }
                if (!parts[2].empty())
                {
                    face[corner].normal = normals[ObjIndex(parts[2], normals.size())];
                }
            }
            const Vec3 faceNormal = Cross(Subtract(face[1].position, face[0].position),
                                          Subtract(face[2].position, face[0].position));
            if (Dot(faceNormal, faceNormal) < 1.0e-12f)
            {
                continue;
            }
            const Vec3 fallback = Unit(faceNormal);
            const Vec3 edge1 = Subtract(face[1].position, face[0].position);
            const Vec3 edge2 = Subtract(face[2].position, face[0].position);
            const Vec2 uv1{face[1].uv[0] - face[0].uv[0], face[1].uv[1] - face[0].uv[1]};
            const Vec2 uv2{face[2].uv[0] - face[0].uv[0], face[2].uv[1] - face[0].uv[1]};
            const float determinant = uv1[0] * uv2[1] - uv1[1] * uv2[0];
            Vec3 tangent{};
            Vec3 bitangent{};
            if (std::abs(determinant) >= 1.0e-12f)
            {
                tangent = Multiply(Subtract(Multiply(edge1, uv2[1]), Multiply(edge2, uv1[1])),
                                   1.0f / determinant);
                bitangent = Multiply(Subtract(Multiply(edge2, uv1[0]), Multiply(edge1, uv2[0])),
                                     1.0f / determinant);
            }
            for (auto &vertex : face)
            {
                if (Dot(vertex.normal, vertex.normal) < 1.0e-12f)
                {
                    vertex.normal = fallback;
                }
                else
                {
                    vertex.normal = Unit(vertex.normal);
                }
                if (Dot(Cross(tangent, vertex.normal), Cross(tangent, vertex.normal)) < 1.0e-10f)
                    vertex.tangent = Unit(Cross(std::abs(vertex.normal[2]) < 0.9f
                                                    ? Vec3{0, 0, 1}
                                                    : Vec3{0, 1, 0},
                                                vertex.normal));
                else
                    vertex.tangent = Unit(tangent);
                vertex.binormalSign = Dot(Cross(vertex.normal, vertex.tangent), bitangent) < 0
                                          ? -1.0f
                                          : 1.0f;
                vertex.authoredTangent = true;
                active->indices.push_back(static_cast<std::uint32_t>(active->vertices.size()));
                active->vertices.push_back(vertex);
            }
        }
    }
    return surfaces;
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

std::vector<BrushModel> ReadBrushModels(const Json &world, const std::filesystem::path &root,
                                        const VisibilityGroups &visibility,
                                        std::size_t &staticModelCount)
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

    std::unordered_map<std::string, std::vector<Surface>> modelCache;
    std::size_t referencedModelCount = 0;
    std::size_t instanceIndex = 0;
    for (const auto &instance : world.value("models", Json::array()))
    {
        const std::string modelName = instance.at("model").get<std::string>();
        if (!modelName.empty() && modelName.front() == ',')
        {
            ++referencedModelCount;
            ++instanceIndex;
            continue;
        }

        auto found = modelCache.find(modelName);
        if (found == modelCache.end())
        {
            const auto modelPath =
                SafeChild(root, std::filesystem::path("xmodel") / (modelName + ".json"));
            const Json model = ReadJson(modelPath);
            if (!model.contains("lods") || model.at("lods").empty())
            {
                throw std::runtime_error("IW3 static model has no render LOD: " + modelName);
            }
            const auto objPath =
                SafeChild(root, model.at("lods").at(0).at("file").get<std::string>());
            found = modelCache.emplace(modelName, ReadObj(objPath)).first;
        }

        const Vec3 origin = ReadVector<3>(instance.at("origin"));
        const float scale = instance.at("scale").get<float>();
        if (!std::isfinite(scale) || scale <= 0.0f)
        {
            throw std::runtime_error("invalid IW3 static-model scale");
        }
        std::array<Vec3, 3> axis{};
        for (std::size_t row = 0; row < 3; ++row)
        {
            axis[row] = ReadVector<3>(instance.at("axis").at(row));
        }
        const auto engineVector = [](const Vec3 &value) {
            return Vec3{value[0], -value[2], value[1]};
        };
        const auto transform = [&](const Vec3 &value) {
            Vec3 result{};
            for (std::size_t component = 0; component < 3; ++component)
            {
                for (std::size_t row = 0; row < 3; ++row)
                {
                    result[component] += axis[row][component] * value[row];
                }
            }
            return result;
        };

        for (const Surface &cached : found->second)
        {
            Surface surface = cached;
            surface.visibilityGroup = visibility.models.at(instanceIndex);
            const auto reflectionProbe = instance.at("reflection_probe").get<unsigned>();
            if (reflectionProbe > UINT16_MAX)
                throw std::runtime_error(
                    "IW3 static-model reflection-probe index exceeds native width");
            surface.reflectionProbe = static_cast<std::uint16_t>(reflectionProbe);
            for (Vertex &vertex : surface.vertices)
            {
                vertex.position =
                    Add(Multiply(transform(engineVector(vertex.position)), scale), origin);
                vertex.normal = Unit(transform(engineVector(vertex.normal)));
                vertex.tangent = Unit(transform(engineVector(vertex.tangent)));
            }
            AlignWinding(surface);
            output.front().surfaces.push_back(std::move(surface));
        }
        ++staticModelCount;
        ++instanceIndex;
    }

    if (referencedModelCount != 0)
    {
        zt::info("iw3: skipped %zu comma-prefixed external xmodel instances",
                 referencedModelCount);
    }
    return output;
}

std::uint32_t PackedNormal(const Vec3 &source, const Vec3 &sourceTangent,
                           const float binormalSign, const bool authoredTangent)
{
    const Vec3 normal = Unit(source);
    Vec3 tangent = authoredTangent ? sourceTangent
                                   : Cross(std::abs(normal[2]) < 0.9f ? Vec3{0, 0, 1}
                                                                    : Vec3{0, 1, 0},
                                           normal);
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
    const std::string suffix = kind == SurfaceKind::cutout ? "_foliage"
                              : kind == SurfaceKind::glass ? "_glass"
                                                          : "_sky";
    for (std::size_t index = 0; index < plan.additionalMaterials.size(); ++index)
        if (plan.additionalMaterials[index].at("material").get<std::string>().ends_with(suffix))
            return static_cast<unsigned>(index + 1);
    throw std::runtime_error("IW3 render plan is missing a material variant");
}

Vec2 EncodedLightmap(const Vertex &vertex, const Surface &surface,
                     const MaterialPlan &material, const RenderPlan &plan)
{
    float x = 0, y = 0;
    const bool baked = surface.lightmap >= 0 &&
                       static_cast<std::size_t>(surface.lightmap) < plan.lightmaps.size();
    if (baked)
    {
        const auto &rectangle = plan.lightmaps[surface.lightmap];
        x = (rectangle.x + std::clamp(vertex.lightmapUv[0] * rectangle.width, 0.5f,
                                      rectangle.width - 0.5f)) /
            4096.0f;
        y = (rectangle.y + std::clamp(vertex.lightmapUv[1] * rectangle.height, 0.5f,
                                      rectangle.height - 0.5f)) /
            4096.0f;
    }
    const unsigned kind = material.kind == SurfaceKind::cutout ? 3u
                          : material.kind == SurfaceKind::glass ? 2u
                                                               : 0u;
    return {static_cast<float>(material.tile) + 0.25f + x * 0.25f,
            static_cast<float>(kind + material.flags + (baked ? 4u : 0u)) + 0.25f + y * 0.25f};
}

Json BuildRender(const Json &world, const VisibilityGroups &visibility,
                 const std::vector<BrushModel> &models, const RenderPlan &plan,
                 std::size_t &triangleCount)
{
    Json output = {{"schema", 1},
                   {"material", plan.material},
                   {"materialDefinition", plan.materialDefinition},
                   {"additionalMaterials", plan.additionalMaterials},
                   {"atlasVertexLayout", 3},
                   {"brushModels", Json::array()},
                   {"reflectionProbes", Json::array()},
                   {"surfaces", Json::array()}};
    std::vector<std::set<unsigned>> treeSurfaces(visibility.treeBounds.size());
    std::set<unsigned> globalSurfaces;
    struct ProbeBounds
    {
        Vec3 minimum{std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::infinity(),
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
        const std::array<SkyMapping, 6> mappings{
            [](float u, float v) { return Vec3{1, -v, -u}; },
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
                positions[index] = Multiply(mappings[face](uvs[index][0] * 2 - 1,
                                                           uvs[index][1] * 2 - 1),
                                            32768.0f);
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
        output["reflectionProbes"].push_back(
            {{"origin", probe.origin},
             {"volume", {minimum, maximum}},
             {"image", probe.image},
             {"sh", probe.sh}});
    }
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
                        std::vector<std::uint32_t> &brushes,
                        std::vector<std::uint8_t> &visited)
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
        result.models.push_back(
            {ReadVector<3>(model.at("mins")), ReadVector<3>(model.at("maxs"))});
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
        const std::uint32_t contents =
            ConvertContents(brush.at("contents").get<std::uint32_t>());
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
        result.hulls.push_back(
            {HullFromPlanes(planes), contents, owner.at(brushIndex)});
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

void WriteCollision(const std::filesystem::path &path, const CollisionData &collision)
{
    std::vector<std::uint8_t> output{'M', 'W', 'C', 'O', 'L', 'L', '0', '4'};
    const auto append = [&](const auto &value) {
        const std::size_t offset = output.size();
        output.resize(offset + sizeof(value));
        std::memcpy(output.data() + offset, &value, sizeof(value));
    };
    append(static_cast<std::uint32_t>(collision.hulls.size()));
    append(static_cast<std::uint32_t>(collision.models.size()));
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
        const auto destination =
            prepared / "images" / ("compass_map_" + targetMap + extension);
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(iterator->path(), destination,
                                   std::filesystem::copy_options::overwrite_existing);
        zt::info("iw3: included HUD minimap %s", destination.filename().string().c_str());
        return;
    }
}
} // namespace

PreparedMap::PreparedMap(PreparedMap &&other) noexcept
    : root(std::move(other.root))
    , collision(std::move(other.collision))
    , scratch(std::move(other.scratch))
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
        scratch = std::move(other.scratch);
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
    result.scratch = MakeScratchDirectory();
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
    const std::string sourceMap = sourceAsset.ends_with(".d3dbsp")
                                      ? sourceAsset.substr(0, sourceAsset.size() - 8)
                                      : sourceAsset;
    const Json world = ReadJson(worldPath);
    const Json collision = ReadJson(collisionPath);
    const Json commonWorld = ReadJson(commonWorldPath);
    if (world.at("schema") != 1 || collision.at("schema") != 1 ||
        commonWorld.at("schema") != 1)
    {
        throw std::runtime_error("unsupported ReplayMapDumpers export schema");
    }
    if (world.at("name").get<std::string>() != collision.at("name").get<std::string>() ||
        world.at("name").get<std::string>() != commonWorld.at("name").get<std::string>())
    {
        throw std::runtime_error("IW3 world, collision, and common-world exports name different maps");
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

    const VisibilityGroups visibility = ReadVisibilityGroups(world);
    std::size_t staticModels = 0;
    std::vector<BrushModel> brushModels =
        ReadBrushModels(world, exportRoot, visibility, staticModels);
    std::vector<std::string> materialNames;
    for (const BrushModel &model : brushModels)
        for (const Surface &surface : model.surfaces)
            materialNames.push_back(surface.material);
    const auto mapDirectory = result.root / "maps" / "mp";
    std::filesystem::create_directories(mapDirectory);
    const RenderPlan renderPlan =
        PrepareRenderAssets(exportRoot, world, materialNames, options.searchPaths, mapDirectory,
                            options.map);
    std::size_t triangles = 0;
    const Json render = BuildRender(world, visibility, brushModels, renderPlan, triangles);
    const auto nativeCollision = ReadCollision(collision);

    const std::string targetAsset = options.map + ".d3dbsp";
    WriteJson(mapDirectory / (targetAsset + ".render.json"), render);
    WriteJson(mapDirectory / (targetAsset + ".lighting.json"),
              BuildLighting(world, commonWorld, entities));
    PrepareNativeLightGrid(exportRoot, sourceAsset,
                           mapDirectory / (targetAsset + ".gpulightgrid.bin"));

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
    result.collision = result.scratch / "collision.bin";
    WriteCollision(result.collision, nativeCollision);
    CopyCompass(extracted, result.root, sourceMap, options.map);

    zt::info("iw3: normalized %zu triangles, %zu collision hulls, %zu brush models and %zu static models",
             triangles, nativeCollision.hulls.size(), nativeCollision.models.size(), staticModels);
    zt::info("iw3: preserved %zu source materials and %zu authored lightmaps",
             renderPlan.materials.size(), renderPlan.lightmaps.size());
    std::size_t portalCount = 0;
    for (const auto &cell : world.at("dpvs").at("cells"))
        portalCount += cell.at("portals").size();
    zt::info("iw3: preserved %zu DPVS cells, %zu AABB trees (%zu populated), %zu planes and %zu portals",
             visibility.cellTrees.size(), visibility.sourceTreeCount, visibility.treeBounds.size(),
             world.at("dpvs").at("planes").size(), portalCount);
    return result;
}
} // namespace iw3
