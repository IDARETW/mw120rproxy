#include "zonetool/dumpsrc/image_dump.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace
{
uint32_t read32(const std::vector<uint8_t> &bytes, const size_t offset)
{
    uint32_t value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
}

int main(const int argc, char **argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("usage: compass-rotation-smoke <source DDS> <rotated DDS>");
        std::ifstream input(argv[1], std::ios::binary);
        const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
        if (bytes.size() < 128 || std::memcmp(bytes.data(), "DDS ", 4) ||
            read32(bytes, 84) != 0x31545844 || read32(bytes, 28) != 1)
            throw std::runtime_error("expected a one-mip DXT1 DDS");

        dumpimg::ImageDumpFile source;
        source.loaded = true;
        source.name = "compass_map_test";
        source.width = static_cast<int32_t>(read32(bytes, 16));
        source.height = static_cast<int32_t>(read32(bytes, 12));
        source.depth = 1;
        source.mipLevels = 1;
        source.format = 0x31545844;
        source.semantic = 2;
        source.ddsPayload = true;
        source.pixels.assign(bytes.begin() + 128, bytes.end());
        const auto original = dumpimg::convertImage(source, 0);
        const auto rotated = dumpimg::convertImage(source, 90);
        if (original.pixels != source.pixels ||
            rotated.pixels.size() != original.pixels.size() ||
            rotated.pixels == original.pixels ||
            rotated.width != original.height || rotated.height != original.width)
            throw std::runtime_error("compass rotation failed its resident-pixel contract");

        std::vector<uint8_t> output(bytes.begin(), bytes.begin() + 128);
        const uint32_t width = rotated.width, height = rotated.height;
        std::memcpy(output.data() + 12, &height, sizeof(height));
        std::memcpy(output.data() + 16, &width, sizeof(width));
        output.insert(output.end(), rotated.pixels.begin(), rotated.pixels.end());
        std::ofstream file(argv[2], std::ios::binary);
        file.write(reinterpret_cast<const char *>(output.data()), output.size());
        if (!file)
            throw std::runtime_error("cannot write rotated DDS");
        std::cout << "rotated " << width << 'x' << height << " BC1 pixels without changing size\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
