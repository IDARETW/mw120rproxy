#include "iw3_lightgrid.h"

#include "common/fs_util.h"
#include "common/json.hpp"
#include "common/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace iw3
{
namespace
{
using Json = nlohmann::json;

constexpr std::string_view Magic = "IW8GLG01";
constexpr float ReplayIrradianceScale = 32.0f;
constexpr float SqrtThree = 1.7320508075688772f;
constexpr float SqrtFive = 2.2360679774997897f;

using Probe = std::array<std::array<float, 9>, 3>;

struct Entry
{
    std::uint16_t color{};
    std::uint8_t primaryLight{};
    std::uint8_t traceMask{};
};

struct Cell
{
    std::array<std::int32_t, 3> position{};
    Entry entry{};
};

template <typename T> T Read(const std::span<const std::uint8_t> data, const std::size_t offset)
{
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("truncated IW3 light-grid data");
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

template <typename T> void Append(std::vector<std::uint8_t> &output, const T &value)
{
    const auto *data = reinterpret_cast<const std::uint8_t *>(&value);
    output.insert(output.end(), data, data + sizeof(value));
}

void AppendBytes(std::vector<std::uint8_t> &output, const void *value, const std::size_t size)
{
    const auto *data = static_cast<const std::uint8_t *>(value);
    output.insert(output.end(), data, data + size);
}

std::uint16_t FloatToHalf(const float value)
{
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t exponent = (bits >> 23) & 0xFFu;
    std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent == 0xFFu)
        return static_cast<std::uint16_t>(sign | (mantissa ? 0x7E00u : 0x7C00u));

    const int adjusted = static_cast<int>(exponent) - 127 + 15;
    if (adjusted >= 31)
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (adjusted <= 0)
    {
        if (adjusted < -10)
            return static_cast<std::uint16_t>(sign);
        mantissa |= 0x800000u;
        const unsigned shift = static_cast<unsigned>(14 - adjusted);
        const std::uint32_t rounded =
            (mantissa + ((1u << (shift - 1)) - 1u) + ((mantissa >> shift) & 1u)) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }

    mantissa += 0xFFFu + ((mantissa >> 13) & 1u);
    if (mantissa & 0x800000u)
    {
        mantissa = 0;
        if (adjusted + 1 >= 31)
            return static_cast<std::uint16_t>(sign | 0x7C00u);
        return static_cast<std::uint16_t>(sign | ((adjusted + 1) << 10));
    }
    return static_cast<std::uint16_t>(sign | (adjusted << 10) | (mantissa >> 13));
}

Json ReadJson(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot read IW3 light-grid metadata " + path.string());
    Json value;
    input >> value;
    return value;
}

std::vector<std::uint8_t> ReadArray(const std::filesystem::path &root, const Json &metadata,
                                    const char *key, const std::size_t expected)
{
    const Json &description = metadata.at(key);
    const std::string relative = description.at("file").get<std::string>();
    const auto declared = description.at("bytes").get<std::uint64_t>();
    const std::filesystem::path relativePath(relative);
    if (relative.empty() || relativePath.is_absolute() || relative.find(':') != std::string::npos ||
        relative.find('\\') != std::string::npos ||
        std::ranges::find(relativePath, "..") != relativePath.end() || declared != expected ||
        declared > 64ull * 1024 * 1024)
        throw std::runtime_error(std::string("invalid IW3 light-grid ") + key + " descriptor");

    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error)
        throw std::runtime_error("cannot resolve IW3 light-grid root");
    const auto path = std::filesystem::weakly_canonical(root / relativePath, error);
    const auto &rootText = canonicalRoot.native();
    const auto &pathText = path.native();
    const bool rootPrefix = !error && pathText.size() >= rootText.size() &&
                            std::equal(rootText.begin(), rootText.end(), pathText.begin(),
                                       [](const wchar_t left, const wchar_t right) {
                                           return std::towlower(left) == std::towlower(right);
                                       });
    const bool componentBoundary =
        rootPrefix && (pathText.size() == rootText.size() ||
                       pathText[rootText.size()] == std::filesystem::path::preferred_separator);
    if (!componentBoundary)
        throw std::runtime_error(std::string("unsafe IW3 light-grid ") + key + " path");

    std::vector<std::uint8_t> data;
    if (!zt::read_file(path.string(), data) || data.size() != expected)
        throw std::runtime_error(std::string("incorrect IW3 light-grid ") + key + " length");
    return data;
}

std::vector<Cell> ReadCells(const std::filesystem::path &root, const Json &metadata,
                            std::vector<std::uint8_t> &palette)
{
    const int rowAxis = metadata.at("row_axis").get<int>();
    const int columnAxis = metadata.at("col_axis").get<int>();
    if (!((rowAxis == 0 && columnAxis == 1) || (rowAxis == 1 && columnAxis == 0)))
        throw std::runtime_error("unsupported IW3 light-grid axes");

    std::array<int, 3> minimum{};
    std::array<int, 3> maximum{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        minimum[axis] = metadata.at("mins").at(axis).get<int>();
        maximum[axis] = metadata.at("maxs").at(axis).get<int>();
        if (minimum[axis] < 0 || maximum[axis] < minimum[axis] || maximum[axis] > 65535)
            throw std::runtime_error("invalid IW3 light-grid bounds");
    }
    const std::size_t rows = static_cast<std::size_t>(maximum[rowAxis] - minimum[rowAxis] + 1);
    const std::size_t entryCount = metadata.at("entry_count").get<std::size_t>();
    const std::size_t colorCount = metadata.at("color_count").get<std::size_t>();
    if (metadata.at("row_count").get<std::size_t>() != rows || entryCount > 8'388'608 ||
        colorCount > 65'536)
        throw std::runtime_error("invalid IW3 light-grid counts");

    const auto starts = ReadArray(root, metadata, "row_starts", rows * 2);
    const auto rawBytes = metadata.at("row_data").at("bytes").get<std::size_t>();
    const auto raw = ReadArray(root, metadata, "row_data", rawBytes);
    const auto entriesRaw = ReadArray(root, metadata, "entries", entryCount * 4);
    palette = ReadArray(root, metadata, "colors", colorCount * 168);

    std::vector<Entry> entries(entryCount);
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        entries[index].color = Read<std::uint16_t>(entriesRaw, index * 4);
        entries[index].primaryLight = entriesRaw[index * 4 + 2];
        entries[index].traceMask = entriesRaw[index * 4 + 3];
        if (entries[index].color >= colorCount)
            throw std::runtime_error("IW3 light-grid entry references a missing color");
    }

    std::vector<Cell> cells;
    cells.reserve(entryCount);
    std::size_t expectedEntry = 0;
    std::size_t previousEnd = 0;
    for (std::size_t row = 0; row < rows; ++row)
    {
        const std::uint16_t start = Read<std::uint16_t>(starts, row * 2);
        if (start == 0xFFFFu)
            continue;
        std::size_t cursor = static_cast<std::size_t>(start) * 4;
        if (cursor < previousEnd || cursor > raw.size() || 12 > raw.size() - cursor)
            throw std::runtime_error("overlapping or truncated IW3 light-grid row");
        const std::uint16_t firstColumn = Read<std::uint16_t>(raw, cursor);
        const std::uint16_t width = Read<std::uint16_t>(raw, cursor + 2);
        const std::uint16_t bottom = Read<std::uint16_t>(raw, cursor + 4);
        const std::uint16_t height = Read<std::uint16_t>(raw, cursor + 6);
        const std::uint32_t firstEntry = Read<std::uint32_t>(raw, cursor + 8);
        cursor += 12;
        if (firstEntry != expectedEntry || !width || !height ||
            firstColumn < minimum[columnAxis] || firstColumn + width - 1 > maximum[columnAxis] ||
            bottom < minimum[2] || bottom + height - 1 > maximum[2])
            throw std::runtime_error("invalid IW3 light-grid row range");

        std::size_t consumed = 0;
        while (consumed < width)
        {
            if (cursor > raw.size() || 2 > raw.size() - cursor)
                throw std::runtime_error("truncated IW3 light-grid column run");
            const std::uint8_t run = raw[cursor++];
            const std::uint8_t count = raw[cursor++];
            if (!run || consumed + run > width)
                throw std::runtime_error("invalid IW3 light-grid column run");
            std::uint8_t startZ = 0;
            if (count)
            {
                if (cursor == raw.size())
                    throw std::runtime_error("missing IW3 light-grid column height");
                startZ = raw[cursor++];
                if (startZ + count > height || expectedEntry + std::size_t(run) * count > entryCount)
                    throw std::runtime_error("IW3 light-grid column exceeds its row");
            }
            for (unsigned column = 0; column < run; ++column)
            {
                for (unsigned z = 0; z < count; ++z)
                {
                    std::array<int, 3> coordinate{0, 0, bottom + startZ + static_cast<int>(z)};
                    coordinate[rowAxis] = minimum[rowAxis] + static_cast<int>(row);
                    coordinate[columnAxis] = firstColumn + static_cast<int>(consumed + column);
                    Cell cell;
                    cell.position = {coordinate[0] * 32 - 131072,
                                     coordinate[1] * 32 - 131072,
                                     coordinate[2] * 64 - 131072};
                    cell.entry = entries[expectedEntry++];
                    cells.push_back(cell);
                }
            }
            consumed += run;
        }
        previousEnd = (cursor + 3) & ~std::size_t{3};
        if (previousEnd > raw.size() ||
            std::any_of(raw.begin() + cursor, raw.begin() + previousEnd,
                        [](const std::uint8_t value) { return value != 0; }))
            throw std::runtime_error("nonzero or truncated IW3 light-grid row padding");
    }
    if (expectedEntry != entryCount || previousEnd != raw.size())
        throw std::runtime_error("unconsumed IW3 light-grid data");
    return cells;
}

std::array<float, 9> EvaluateShBasis(const std::array<float, 3> &direction)
{
    const auto [x, y, z] = direction;
    return {0.2820947918f,
            -0.4886025119f * y,
            0.4886025119f * z,
            -0.4886025119f * x,
            1.0925484306f * x * y,
            -1.0925484306f * y * z,
            0.3153915653f * (3.0f * z * z - 1.0f),
            -1.0925484306f * x * z,
            0.5462742153f * (x * x - y * y)};
}

const std::array<std::array<double, 56>, 9> &LightGridProjection()
{
    static const auto projection = [] {
        constexpr std::array<std::array<float, 3>, 3> rotation{{
            {0.47140452f, 0.0f, 0.33333334f},
            {-0.23570226f, 0.40824828f, 0.33333334f},
            {-0.23570226f, -0.40824828f, 0.33333334f}}};
        std::array<std::array<double, 9>, 56> samples{};
        std::size_t sampleIndex = 0;
        for (int z = 0; z < 4; ++z)
        {
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 4; ++x)
                {
                    if (x > 0 && x < 3 && y > 0 && y < 3 && z > 0 && z < 3)
                        continue;
                    const std::array<float, 3> delta{
                        x * (2.0f / 3.0f) - 1.0f,
                        y * (2.0f / 3.0f) - 1.0f,
                        z * (2.0f / 3.0f) - 1.0f};
                    std::array<float, 3> direction{};
                    for (unsigned component = 0; component < 3; ++component)
                    {
                        for (unsigned axis = 0; axis < 3; ++axis)
                            direction[component] += delta[axis] * rotation[component][axis];
                    }
                    const float length = std::sqrt(direction[0] * direction[0] +
                                                   direction[1] * direction[1] +
                                                   direction[2] * direction[2]);
                    if (!(length > 0.0f) || sampleIndex >= samples.size())
                        throw std::runtime_error("invalid IW3 light-grid sample basis");
                    for (float &component : direction)
                        component /= length;
                    const auto basis = EvaluateShBasis(direction);
                    for (unsigned coefficient = 0; coefficient < 9; ++coefficient)
                        samples[sampleIndex][coefficient] = basis[coefficient];
                    ++sampleIndex;
                }
            }
        }
        if (sampleIndex != samples.size())
            throw std::runtime_error("incorrect IW3 light-grid sample count");

        std::array<std::array<double, 18>, 9> inverse{};
        for (unsigned row = 0; row < 9; ++row)
        {
            for (unsigned column = 0; column < 9; ++column)
                for (const auto &sample : samples)
                    inverse[row][column] += sample[row] * sample[column];
            inverse[row][row + 9] = 1.0;
        }
        for (unsigned column = 0; column < 9; ++column)
        {
            unsigned pivot = column;
            for (unsigned row = column + 1; row < 9; ++row)
                if (std::abs(inverse[row][column]) > std::abs(inverse[pivot][column]))
                    pivot = row;
            if (std::abs(inverse[pivot][column]) < 1.0e-10)
                throw std::runtime_error("singular IW3 light-grid SH projection");
            if (pivot != column)
                std::swap(inverse[pivot], inverse[column]);
            const double divisor = inverse[column][column];
            for (double &value : inverse[column])
                value /= divisor;
            for (unsigned row = 0; row < 9; ++row)
            {
                if (row == column)
                    continue;
                const double factor = inverse[row][column];
                for (unsigned entry = 0; entry < 18; ++entry)
                    inverse[row][entry] -= factor * inverse[column][entry];
            }
        }

        std::array<std::array<double, 56>, 9> result{};
        for (unsigned coefficient = 0; coefficient < 9; ++coefficient)
            for (unsigned sample = 0; sample < 56; ++sample)
                for (unsigned term = 0; term < 9; ++term)
                    result[coefficient][sample] +=
                        inverse[coefficient][term + 9] * samples[sample][term];

        for (unsigned expected = 0; expected < 9; ++expected)
        {
            for (unsigned actual = 0; actual < 9; ++actual)
            {
                double value = 0.0;
                for (unsigned sample = 0; sample < 56; ++sample)
                    value += result[actual][sample] * samples[sample][expected];
                if (std::abs(value - (actual == expected ? 1.0 : 0.0)) > 1.0e-5)
                    throw std::runtime_error("invalid IW3 light-grid SH projection");
            }
        }
        return result;
    }();
    return projection;
}

std::vector<Probe> BuildProbes(const std::vector<std::uint8_t> &palette)
{
    std::vector<Probe> probes(palette.size() / 168);
    const auto &projection = LightGridProjection();
    constexpr float sourceScale = ReplayIrradianceScale / (255.0f * 3.1415926535897932f);
    for (std::size_t probe = 0; probe < probes.size(); ++probe)
    {
        for (unsigned channel = 0; channel < 3; ++channel)
        {
            for (unsigned coefficient = 0; coefficient < 9; ++coefficient)
            {
                double value = 0.0;
                for (unsigned sample = 0; sample < 56; ++sample)
                    value += projection[coefficient][sample] *
                             palette[probe * 168 + sample * 3 + channel];
                probes[probe][channel][coefficient] =
                    static_cast<float>(value * sourceScale);
            }
        }
    }
    return probes;
}

std::array<std::uint8_t, 32> PackProbe(const Probe &probe)
{
    for (const auto &channel : probe)
        for (const float value : channel)
            if (!std::isfinite(value) || std::abs(value) > 60000.0f)
                throw std::runtime_error("invalid IW3 diffuse probe coefficient");
    if (probe[0][0] < 0.0f || probe[1][0] < 0.0f || probe[2][0] < 0.0f)
        throw std::runtime_error("negative IW3 diffuse probe intensity");
    const auto red = FloatToHalf(probe[0][0]);
    const auto green = FloatToHalf(probe[1][0]);
    const auto blue = FloatToHalf(probe[2][0]);
    const std::uint32_t dc = (red >> 4) | (std::uint32_t(green >> 4) << 11) |
                             (std::uint32_t(blue >> 5) << 22);
    std::array<std::uint8_t, 32> packed{};
    std::memcpy(packed.data(), &dc, sizeof(dc));
    for (unsigned channel = 0; channel < 3; ++channel)
    {
        const float dcValue = probe[channel][0];
        for (unsigned coefficient = 1; coefficient < 9; ++coefficient)
        {
            float normalized = 0.0f;
            if (dcValue > 0.0f)
            {
                const float range = coefficient < 4 ? SqrtThree : SqrtFive;
                normalized = std::clamp(probe[channel][coefficient] / (dcValue * range),
                                        -1.0f, 1.0f);
            }
            packed[4 + channel * 8 + coefficient - 1] =
                static_cast<std::uint8_t>(normalized * 127.0f + 127.5f);
        }
    }
    packed[29] = 255;
    return packed;
}

float Median(std::vector<float> values)
{
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2)
        return values[middle];
    const float high = values[middle];
    std::nth_element(values.begin(), values.begin() + middle - 1, values.begin() + middle);
    return (values[middle - 1] + high) * 0.5f;
}

std::vector<std::uint8_t> BuildPayload(std::vector<Cell> cells,
                                       const std::vector<std::uint8_t> &palette)
{
    if (cells.empty() || cells.size() > 2'000'000 || palette.empty() || palette.size() % 168)
        throw std::runtime_error("invalid IW3 light-grid probe or palette count");
    std::ranges::sort(cells, {}, &Cell::position);
    std::map<std::array<std::int32_t, 3>, std::uint32_t> lookup;
    std::array<std::int32_t, 3> low = cells.front().position;
    std::array<std::int32_t, 3> high = cells.front().position;
    for (std::uint32_t index = 0; index < cells.size(); ++index)
    {
        const auto &position = cells[index].position;
        if (std::abs(position[0]) > 100000 || std::abs(position[1]) > 100000 ||
            std::abs(position[2]) > 100000 || position[0] % 32 || position[1] % 32 ||
            position[2] % 64 || !lookup.emplace(position, index).second)
            throw std::runtime_error("invalid, duplicate, or off-lattice IW3 light-grid probe");
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            low[axis] = std::min(low[axis], position[axis]);
            high[axis] = std::max(high[axis], position[axis]);
        }
    }
    std::array<std::uint32_t, 3> dimensions{};
    std::uint64_t voxelCount = 1;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        dimensions[axis] = static_cast<std::uint32_t>((high[axis] - low[axis]) / 32);
        if (!dimensions[axis])
            throw std::runtime_error("unsupported IW3 light-grid extent");
        voxelCount *= dimensions[axis];
    }
    if (voxelCount > 8'000'000)
        throw std::runtime_error("IW3 light-grid voxel count exceeds Replay limits");

    std::vector<std::uint32_t> voxels(static_cast<std::size_t>(voxelCount), UINT32_MAX);
    std::vector<std::array<std::uint32_t, 4>> tetrahedra;
    constexpr std::array<std::array<unsigned, 3>, 6> permutations{{
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
    for (const Cell &cell : cells)
    {
        std::array<std::uint32_t, 8> corners{};
        bool complete = true;
        for (unsigned mask = 0; mask < 8; ++mask)
        {
            const std::array<std::int32_t, 3> position{
                cell.position[0] + ((mask & 1) ? 32 : 0),
                cell.position[1] + ((mask & 2) ? 32 : 0),
                cell.position[2] + ((mask & 4) ? 64 : 0)};
            const auto found = lookup.find(position);
            if (found == lookup.end())
            {
                complete = false;
                break;
            }
            corners[mask] = found->second;
        }
        if (!complete)
            continue;
        if (tetrahedra.size() > 8'000'000 - permutations.size())
            throw std::runtime_error("IW3 light-grid tetrahedra exceed Replay limits");
        const auto first = static_cast<std::uint32_t>(tetrahedra.size());
        for (const auto &order : permutations)
        {
            unsigned mask = 0;
            std::array<std::uint32_t, 4> tetrahedron{corners[0], 0, 0, 0};
            for (unsigned step = 0; step < 3; ++step)
            {
                mask |= 1u << order[step];
                tetrahedron[step + 1] = corners[mask];
            }
            tetrahedra.push_back(tetrahedron);
        }
        const std::uint32_t x = static_cast<std::uint32_t>((cell.position[0] - low[0]) / 32);
        const std::uint32_t y = static_cast<std::uint32_t>((cell.position[1] - low[1]) / 32);
        const std::uint32_t z = static_cast<std::uint32_t>((cell.position[2] - low[2]) / 32);
        if (x >= dimensions[0] || y >= dimensions[1] || z + 1 >= dimensions[2])
            throw std::runtime_error("complete IW3 light-grid cell exceeds its voxel extent");
        const std::size_t voxel =
            (static_cast<std::size_t>(y) * dimensions[0] + x) * dimensions[2] + z;
        voxels[voxel] = first;
        voxels[voxel + 1] = first;
    }
    if (tetrahedra.empty())
        throw std::runtime_error("IW3 light-grid has no complete source cells");

    std::vector<std::array<std::uint32_t, 4>> neighbors(tetrahedra.size(),
                                                        {UINT32_MAX, UINT32_MAX, UINT32_MAX,
                                                         UINT32_MAX});
    std::map<std::array<std::uint32_t, 3>, std::pair<std::uint32_t, unsigned>> faces;
    for (std::uint32_t index = 0; index < tetrahedra.size(); ++index)
    {
        for (unsigned face = 0; face < 4; ++face)
        {
            std::array<std::uint32_t, 3> key{};
            for (unsigned source = 0, target = 0; source < 4; ++source)
                if (source != face)
                    key[target++] = tetrahedra[index][source];
            std::ranges::sort(key);
            const auto found = faces.find(key);
            if (found == faces.end())
            {
                faces.emplace(key, std::pair{index, face});
            }
            else
            {
                const auto [otherIndex, otherFace] = found->second;
                neighbors[index][face] = otherIndex;
                neighbors[otherIndex][otherFace] = index;
                faces.erase(found);
            }
        }
    }

    const auto probes = BuildProbes(palette);
    std::array<float, 32> fallback{};
    for (unsigned channel = 0; channel < 3; ++channel)
    {
        for (unsigned coefficient = 0; coefficient < 9; ++coefficient)
        {
            std::vector<float> occupied;
            occupied.reserve(cells.size());
            for (const Cell &cell : cells)
                occupied.push_back(probes[cell.entry.color][channel][coefficient]);
            fallback[channel * 9 + coefficient] = Median(std::move(occupied));
        }
    }
    fallback[27] = 1.0f;
    fallback[28] = 1.0f;

    std::vector<std::uint8_t> output;
    const std::uint32_t probeCount = static_cast<std::uint32_t>(cells.size());
    const std::uint32_t tetrahedronCount = static_cast<std::uint32_t>(tetrahedra.size());
    const std::uint32_t rootCount = dimensions[0] * dimensions[1];
    output.reserve(264ull + probeCount * 44ull + tetrahedronCount * 32ull +
                   rootCount * 12ull + voxelCount * 4ull);
    AppendBytes(output, Magic.data(), Magic.size());
    Append(output, probeCount);
    Append(output, tetrahedronCount);
    Append(output, rootCount);
    Append(output, static_cast<std::uint32_t>(voxelCount));

    constexpr float diagonal = 0.5773502691896258f;
    constexpr std::array<std::array<float, 3>, 7> axes{{
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {diagonal, diagonal, diagonal},
        {diagonal, -diagonal, diagonal}, {-diagonal, diagonal, diagonal},
        {-diagonal, -diagonal, diagonal}}};
    for (const auto &axis : axes)
    {
        float minimum = 0.0f;
        float maximum = 0.0f;
        for (unsigned component = 0; component < 3; ++component)
        {
            minimum += axis[component] * (axis[component] >= 0 ? low[component] : high[component]);
            maximum += axis[component] * (axis[component] >= 0 ? high[component] : low[component]);
        }
        Append(output, axis[0]);
        Append(output, axis[1]);
        Append(output, minimum);
        Append(output, maximum);
    }
    for (const float value : fallback)
        Append(output, FloatToHalf(value));

    for (const auto dimension : dimensions)
        Append(output, static_cast<std::int32_t>(dimension));
    for (const std::int32_t value : {0, 5, 3, 1, 0})
        Append(output, value);
    for (const auto value : low)
        Append(output, static_cast<float>(value));
    Append(output, 0.0f);
    for (const auto value : high)
        Append(output, static_cast<float>(value));
    Append(output, 0.0f);

    for (const Cell &cell : cells)
    {
        const auto packed = PackProbe(probes[cell.entry.color]);
        AppendBytes(output, packed.data(), packed.size());
    }
    for (const Cell &cell : cells)
        for (const auto value : cell.position)
            Append(output, static_cast<float>(value));
    for (const auto &tetrahedron : tetrahedra)
        AppendBytes(output, tetrahedron.data(), sizeof(tetrahedron));
    for (const auto &neighbor : neighbors)
        AppendBytes(output, neighbor.data(), sizeof(neighbor));
    for (std::uint32_t y = 0; y < dimensions[1]; ++y)
    {
        for (std::uint32_t x = 0; x < dimensions[0]; ++x)
        {
            Append(output, 0x80000000u | ((y * dimensions[0] + x) * dimensions[2]));
            Append(output, 0u);
            Append(output, dimensions[2] - 1);
        }
    }
    AppendBytes(output, voxels.data(), voxels.size() * sizeof(voxels.front()));
    if (output.size() > 512ull * 1024 * 1024)
        throw std::runtime_error("native Replay light-grid exceeds the supported size");
    zt::info("iw3: converted %u authored light probes into %u native tetrahedra (%ux%ux%u)",
             probeCount, tetrahedronCount, dimensions[0], dimensions[1], dimensions[2]);
    return output;
}
} // namespace

bool PrepareNativeLightGrid(const std::filesystem::path &sourceRoot,
                            const std::string &sourceAsset,
                            const std::filesystem::path &output)
{
    const auto metadataPath =
        sourceRoot / "maps" / "mp" / (sourceAsset + ".lightgrid.json");
    std::error_code error;
    if (!std::filesystem::is_regular_file(metadataPath, error))
    {
        zt::warn("iw3: source map has no exported light grid");
        return false;
    }
    const Json metadata = ReadJson(metadataPath);
    if (metadata.value("available", true) == false)
    {
        zt::warn("iw3: source map has no compiled light grid");
        return false;
    }
    if (metadata.at("schema").get<int>() != 1 ||
        metadata.at("name").get<std::string>() != "maps/mp/" + sourceAsset)
        throw std::runtime_error("mismatched IW3 light-grid export");

    std::vector<std::uint8_t> palette;
    const auto cells = ReadCells(sourceRoot, metadata, palette);
    const auto payload = BuildPayload(cells, palette);
    if (!zt::write_file(output.string(), payload))
        throw std::runtime_error("cannot write temporary native Replay light grid");
    return true;
}
} // namespace iw3
