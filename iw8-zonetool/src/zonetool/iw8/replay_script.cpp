#include "replay_script.h"

#include "iw8_zonebuffer.h"

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace iw8::replay_script
{
namespace
{
// Signed Replay 1.20 Backlot map-main stack/export evidence: first export id ref_0254.
constexpr uint32_t kStartupExport = 0x254;

void appendU32(std::vector<uint8_t> &out, const uint32_t value)
{
    const size_t offset = out.size();
    out.resize(offset + sizeof(value));
    std::memcpy(out.data() + offset, &value, sizeof(value));
}

void appendString(std::vector<uint8_t> &out, const std::string &value)
{
    if (value.find('\0') != std::string::npos)
        throw std::invalid_argument("Replay startup script string contains a NUL");
    out.insert(out.end(), value.begin(), value.end());
    out.push_back(0);
}

void appendF32(std::vector<uint8_t> &out, const float value)
{
    const size_t offset = out.size();
    out.resize(offset + sizeof(value));
    std::memcpy(out.data() + offset, &value, sizeof(value));
}

void writeScriptHeader(ZoneWriter &writer, const uint32_t compressedLength,
                       const uint32_t stackLength, const uint32_t bytecodeLength)
{
    uint8_t header[0x28]{};
    std::memcpy(header + 0x00, &PTR_FOLLOWS, sizeof(PTR_FOLLOWS));
    std::memcpy(header + 0x08, &compressedLength, sizeof(compressedLength));
    std::memcpy(header + 0x0C, &stackLength, sizeof(stackLength));
    std::memcpy(header + 0x10, &bytecodeLength, sizeof(bytecodeLength));
    std::memcpy(header + 0x18, &PTR_FOLLOWS, sizeof(PTR_FOLLOWS));
    std::memcpy(header + 0x20, &PTR_FOLLOWS, sizeof(PTR_FOLLOWS));

    writer.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    writer.align(7);
    writer.write(header, sizeof(header));
    writer.popStream();
}
} // namespace

// The native ScriptFile and RawFile assets store zlib-wrapped DEFLATE data.
// Stored blocks keep the converter self-contained while preserving the exact
// decompressed bytes.
std::vector<uint8_t> zlibStored(const std::vector<uint8_t> &input)
{
    if (input.size() > std::numeric_limits<uint32_t>::max())
        throw std::length_error("Replay zlib payload is too large");

    std::vector<uint8_t> output{0x78, 0x01};
    uint32_t adlerA = 1;
    uint32_t adlerB = 0;
    size_t cursor = 0;
    do
    {
        const size_t remaining = input.size() - cursor;
        const uint16_t count = static_cast<uint16_t>((remaining > 0xFFFF) ? 0xFFFF : remaining);
        const bool final = cursor + count == input.size();
        output.push_back(final ? 0x01 : 0x00);
        output.push_back(static_cast<uint8_t>(count));
        output.push_back(static_cast<uint8_t>(count >> 8));
        const uint16_t inverse = static_cast<uint16_t>(~count);
        output.push_back(static_cast<uint8_t>(inverse));
        output.push_back(static_cast<uint8_t>(inverse >> 8));
        output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(cursor),
                      input.begin() + static_cast<std::ptrdiff_t>(cursor + count));
        for (size_t i = 0; i < count; ++i)
        {
            adlerA = (adlerA + input[cursor + i]) % 65521;
            adlerB = (adlerB + adlerA) % 65521;
        }
        cursor += count;
    } while (cursor != input.size());

    const uint32_t adler = (adlerB << 16) | adlerA;
    output.push_back(static_cast<uint8_t>(adler >> 24));
    output.push_back(static_cast<uint8_t>(adler >> 16));
    output.push_back(static_cast<uint8_t>(adler >> 8));
    output.push_back(static_cast<uint8_t>(adler));
    return output;
}

void emitCompassStartup(ZoneWriter &writer, const std::string &assetName,
                        const std::string &mapName, const float northwestX,
                        const float northwestY, const float southeastX,
                        const float southeastY)
{
    if (assetName.empty() || mapName.empty())
        throw std::invalid_argument("Replay startup script asset and map names are required");
    if (!std::isfinite(northwestX) || !std::isfinite(northwestY) ||
        !std::isfinite(southeastX) || !std::isfinite(southeastY))
        throw std::invalid_argument("Replay compass bounds must be finite");

    const std::string compass = "compass_map_" + mapName;

    // Invoke Replay's native seven-argument setminimap builtin (0x1D3)
    // directly. Values are pushed in reverse argument order by the IW8 VM.
    std::vector<uint8_t> bytecode{0x3B, 0x4B, 0x15, 0x7A, 0x00, 0x00, 0x00, 0x00,
                                  0x16, 0x01, 0x70};
    appendF32(bytecode, southeastY);
    bytecode.push_back(0x70);
    appendF32(bytecode, southeastX);
    bytecode.push_back(0x70);
    appendF32(bytecode, northwestY);
    bytecode.push_back(0x70);
    appendF32(bytecode, northwestX);
    bytecode.insert(bytecode.end(), {0x7A, 0x00, 0x00, 0x00, 0x00,
                                     0x23, 0x07, 0xD3, 0x01, 0x58, 0x3B});
    const uint32_t functionSize = static_cast<uint32_t>(bytecode.size() - 1);

    std::vector<uint8_t> stack;
    stack.reserve(10 + compass.size() * 2);
    appendU32(stack, functionSize);
    appendU32(stack, kStartupExport);
    // Strings appear in bytecode execution order: alternate image, then primary.
    appendString(stack, compass);
    appendString(stack, compass);

    const auto compressed = zlibStored(stack);
    if (compressed.size() > std::numeric_limits<uint32_t>::max() ||
        stack.size() > std::numeric_limits<uint32_t>::max())
        throw std::length_error("Replay startup script payload is too large");

    writeScriptHeader(writer, static_cast<uint32_t>(compressed.size()),
                      static_cast<uint32_t>(stack.size()),
                      static_cast<uint32_t>(bytecode.size()));

    writer.pushStream(XFILE_BLOCK_VIRTUAL);
    writer.writeStr(assetName);
    writer.popStream();

    writer.pushStream(XFILE_BLOCK_RUNTIME);
    writer.write(compressed.data(), compressed.size());
    writer.write(bytecode.data(), bytecode.size());
    writer.popStream();
}
} // namespace iw8::replay_script
