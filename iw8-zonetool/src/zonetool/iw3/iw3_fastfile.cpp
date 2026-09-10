#include "iw3_fastfile.h"

#include "common/fs_util.h"
#include "common/json.hpp"
#include "common/log.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
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
    Vec2 uv{};
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
};

struct Surface
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
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
                                        L"IWI",
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
        vertex.uv = ReadVector<2>(item.at("uv"));
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
            active = nullptr;
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
            {
                surfaces.emplace_back();
                active = &surfaces.back();
            }

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

std::vector<Surface> ReadSurfaces(const Json &world, const std::filesystem::path &root,
                                  std::size_t &staticModelCount)
{
    const auto &sourceSurfaces = world.at("surfaces");
    if (!sourceSurfaces.is_array() || sourceSurfaces.empty())
    {
        throw std::runtime_error("IW3 world contains no render surfaces");
    }

    std::size_t first = 0;
    std::size_t count = sourceSurfaces.size();
    if (world.contains("brush_models") && !world.at("brush_models").empty())
    {
        first = world.at("brush_models").at(0).at("start").get<std::size_t>();
        count = world.at("brush_models").at(0).at("count").get<std::size_t>();
        if (first > sourceSurfaces.size() || count > sourceSurfaces.size() - first)
        {
            throw std::runtime_error("invalid IW3 world brush-model surface range");
        }
    }

    std::vector<Surface> output;
    output.reserve(count);
    for (std::size_t index = first; index < first + count; ++index)
    {
        Surface surface = ReadWorldSurface(sourceSurfaces.at(index));
        AlignWinding(surface);
        output.push_back(std::move(surface));
    }

    std::unordered_map<std::string, std::vector<Surface>> modelCache;
    for (const auto &instance : world.value("models", Json::array()))
    {
        const std::string modelName = instance.at("model").get<std::string>();
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
            for (Vertex &vertex : surface.vertices)
            {
                vertex.position =
                    Add(Multiply(transform(engineVector(vertex.position)), scale), origin);
                vertex.normal = Unit(transform(engineVector(vertex.normal)));
            }
            AlignWinding(surface);
            output.push_back(std::move(surface));
        }
        ++staticModelCount;
    }
    return output;
}

std::uint32_t PackedNormal(const Vec3 &source)
{
    const Vec3 normal = Unit(source);
    const Vec3 tangent =
        Unit(Cross(std::abs(normal[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, normal));
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
    return encoded[0] | (encoded[1] << 10) | (encoded[2] << 20) |
           (static_cast<std::uint32_t>(largest) << 30);
}

Json BuildRender(const std::vector<Surface> &surfaces, std::size_t &triangleCount)
{
    Json output = {{"schema", 1}, {"material", "$default"}, {"surfaces", Json::array()}};
    Json *target = nullptr;
    std::size_t targetVertices = 0;
    std::size_t targetIndices = 0;

    for (const Surface &surface : surfaces)
    {
        std::unordered_map<std::uint32_t, std::uint32_t> remap;
        for (std::size_t offset = 0; offset < surface.indices.size(); offset += 3)
        {
            const Vertex &a = surface.vertices[surface.indices[offset]];
            const Vertex &b = surface.vertices[surface.indices[offset + 1]];
            const Vertex &c = surface.vertices[surface.indices[offset + 2]];
            if (Dot(Cross(Subtract(b.position, a.position), Subtract(c.position, a.position)),
                    Cross(Subtract(b.position, a.position), Subtract(c.position, a.position))) <
                1.0e-12f)
            {
                throw std::runtime_error("degenerate render triangle in IW3 map");
            }
            if (!target || targetVertices + 3 > 60000 || targetIndices + 3 > 65535u * 3u)
            {
                output["surfaces"].push_back(
                    {{"vertices", Json::array()}, {"indices", Json::array()}});
                target = &output["surfaces"].back();
                targetVertices = 0;
                targetIndices = 0;
                remap.clear();
            }
            for (const std::uint32_t sourceIndex :
                 {surface.indices[offset], surface.indices[offset + 2],
                  surface.indices[offset + 1]})
            {
                auto found = remap.find(sourceIndex);
                if (found == remap.end())
                {
                    const Vertex &vertex = surface.vertices[sourceIndex];
                    const std::uint32_t index = static_cast<std::uint32_t>(targetVertices++);
                    (*target)["vertices"].push_back({{"position", vertex.position},
                                                     {"uv", vertex.uv},
                                                     {"normal", PackedNormal(vertex.normal)},
                                                     {"color", vertex.color}});
                    found = remap.emplace(sourceIndex, index).first;
                }
                (*target)["indices"].push_back(found->second);
                ++targetIndices;
            }
            ++triangleCount;
        }
    }
    if (output["surfaces"].empty() || output["surfaces"].size() > 4096)
    {
        throw std::runtime_error("IW3 render geometry exceeds Replay surface limits");
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

std::vector<std::vector<Vec3>> ReadCollision(const Json &collision)
{
    std::vector<std::vector<Vec3>> hulls;
    for (const auto &brush : collision.at("brushes"))
    {
        const std::uint32_t contents = brush.at("contents").get<std::uint32_t>();
        if ((contents & (1u | 0x10000u)) == 0)
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
        hulls.push_back(HullFromPlanes(planes));
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
        std::vector<Vec3> hull;
        hull.reserve(6);
        for (const float direction : {-1.0f, 1.0f})
            for (const Vec3 &point : points)
                hull.push_back(Add(point, Multiply(offset, direction)));
        hulls.push_back(std::move(hull));
    }
    if (hulls.empty() || hulls.size() > 32768)
    {
        throw std::runtime_error("IW3 collision exceeds Replay hull limits");
    }
    return hulls;
}

void WriteCollision(const std::filesystem::path &path, const std::vector<std::vector<Vec3>> &hulls)
{
    std::vector<std::uint8_t> output{'M', 'W', 'C', 'O', 'L', 'L', '0', '2'};
    const auto append = [&](const auto &value) {
        const std::size_t offset = output.size();
        output.resize(offset + sizeof(value));
        std::memcpy(output.data() + offset, &value, sizeof(value));
    };
    append(static_cast<std::uint32_t>(hulls.size()));
    for (const auto &hull : hulls)
    {
        append(static_cast<std::uint32_t>(hull.size()));
        for (const Vec3 &point : hull)
        {
            for (const float value : point)
            {
                if (!std::isfinite(value) || std::abs(value) > 100000.0f)
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

Json BuildLighting(const Json &world, const std::string &entities)
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
            {"up", {0.0f, 0.0f, 0.0f}}};
}

void CopyCompass(const std::filesystem::path &extracted, const std::filesystem::path &prepared,
                 const std::string &sourceMap, const std::string &targetMap)
{
    const std::string sourceName = "compass_map_" + sourceMap + ".iwi";
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(extracted, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file(error) ||
            _stricmp(iterator->path().filename().string().c_str(), sourceName.c_str()) != 0)
            continue;
        const auto destination = prepared / "images" / ("compass_map_" + targetMap + ".iwi");
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
    const auto exportRoot = worldPath.parent_path().parent_path().parent_path();
    const std::string sourceAsset = worldPath.filename().string().substr(
        0, worldPath.filename().string().size() - std::string(".replay-world.json").size());
    const std::string sourceMap = sourceAsset.ends_with(".d3dbsp")
                                      ? sourceAsset.substr(0, sourceAsset.size() - 8)
                                      : sourceAsset;
    const Json world = ReadJson(worldPath);
    const Json collision = ReadJson(collisionPath);
    if (world.at("schema") != 1 || collision.at("schema") != 1)
    {
        throw std::runtime_error("unsupported ReplayMapDumpers export schema");
    }
    if (world.at("name").get<std::string>() != collision.at("name").get<std::string>())
    {
        throw std::runtime_error("IW3 world and collision exports name different maps");
    }

    std::string entities;
    const auto sourceEntities = worldPath.parent_path() / (sourceAsset + ".ents");
    if (!zt::read_file_str(sourceEntities.string(), entities) || entities.empty())
    {
        throw std::runtime_error("IW3 map entity export is missing");
    }

    std::size_t staticModels = 0;
    std::vector<Surface> surfaces = ReadSurfaces(world, exportRoot, staticModels);
    std::size_t triangles = 0;
    const Json render = BuildRender(surfaces, triangles);
    const auto hulls = ReadCollision(collision);

    const auto mapDirectory = result.root / "maps" / "mp";
    std::filesystem::create_directories(mapDirectory);
    const std::string targetAsset = options.map + ".d3dbsp";
    WriteJson(mapDirectory / (targetAsset + ".render.json"), render);
    WriteJson(mapDirectory / (targetAsset + ".lighting.json"), BuildLighting(world, entities));

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
    WriteCollision(result.collision, hulls);
    CopyCompass(extracted, result.root, sourceMap, options.map);

    zt::info("iw3: normalized %zu triangles, %zu collision hulls and %zu static models", triangles,
             hulls.size(), staticModels);
    zt::warn("iw3: direct fastfile import uses Replay's stock material; use build-map with a "
             "prepared material dump when source textures and techsets are required");
    return result;
}
} // namespace iw3
