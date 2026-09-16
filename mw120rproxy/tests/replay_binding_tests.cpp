#include "../../iw8-zonetool/src/zonetool/iw8/replay_render.h"
#include "../../iw8-zonetool/src/common/json.hpp"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void check(const bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<uint8_t> unhex(const std::string& value) {
    check(value.size() % 2 == 0, "Invalid test hex data");
    std::vector<uint8_t> result;
    for (size_t i = 0; i < value.size(); i += 2)
        result.push_back(static_cast<uint8_t>(std::stoul(value.substr(i, 2), nullptr, 16)));
    return result;
}

std::string hex(const std::vector<uint8_t>& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const auto byte : value) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

void writeJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path);
    check(bool(output), "Could not write binding test JSON");
    output << value.dump(2) << '\n';
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path temporary;
    try {
        check(argc == 2, "Pass the prepared Replay render fixture");
        const std::filesystem::path source = argv[1];
        check(std::filesystem::is_regular_file(source), "Replay render fixture is missing");

        const auto candidate = std::filesystem::temp_directory_path() /
                               ("replay_binding_test_" + std::to_string(GetCurrentProcessId()) +
                                "_" + std::to_string(GetTickCount64()));
        check(std::filesystem::create_directory(candidate), "Test directory already exists");
        temporary = candidate;
        const auto sourceDirectory = source.parent_path();
        for (const auto& file : {"mp_test.d3dbsp.render.json", "mp_test.d3dbsp.material.json",
                                 "mp_test.d3dbsp.techset.json"})
            std::filesystem::copy_file(sourceDirectory / file, temporary / file,
                                       std::filesystem::copy_options::overwrite_existing);

        // Keep the test focused on the techset contract; use built-in images so an
        // atlas payload mismatch cannot mask the binding result.
        const auto materialPath = temporary / "mp_test.d3dbsp.material.json";
        std::ifstream materialInput(materialPath);
        auto material = nlohmann::json::parse(materialInput);
        material["imageDefinitions"] = nlohmann::json::array();
        material["textures"].at(0)["image"] = "$gray";
        writeJson(materialPath, material);

        const auto techsetPath = temporary / "mp_test.d3dbsp.techset.json";
        std::ifstream techsetInput(techsetPath);
        auto techset = nlohmann::json::parse(techsetInput);
        const auto renderPath = temporary / "mp_test.d3dbsp.render.json";
        std::ifstream renderInput(renderPath);
        auto render = nlohmann::json::parse(renderInput);
        const auto probePixels = temporary / "binding_probe.rgba";
        {
            std::ofstream output(probePixels, std::ios::binary);
            const std::array<uint8_t, 96> pixels{};
            output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
        }
        nlohmann::json probe;
        probe["origin"] = {0, 0, 0};
        probe["volume"] = {{-1, -1, -1}, {1, 1, 1}};
        probe["image"] = {{"name", "mw120r/binding_probe"},
                          {"width", 1},
                          {"height", 1},
                          {"flags", 0x8000},
                          {"rgba8", "binding_probe.rgba"}};
        probe["sh"] = nlohmann::json::array();
        for (unsigned channel = 0; channel < 4; ++channel)
            probe["sh"].push_back({0, 0, 0, 0, 0, 0, 0, 0, 0});
        render["reflectionProbes"] = nlohmann::json::array({probe});
        render["dpvs"] = {{"planes", nlohmann::json::array()},
                          {"nodes", {0}},
                          {"cells",
                           {{{"bounds", {{-100000, -100000, -100000}, {100000, 100000, 100000}}},
                             {"portals", nlohmann::json::array()},
                             {"trees", nlohmann::json::array()}}}}};
        writeJson(renderPath, render);

        auto brokenTechset = techset;
        auto& forward = brokenTechset.at("techniques").back();
        auto forwardHeader = unhex(forward.at("header"));
        auto forwardArgs = forward.at("args").get<std::string>();
        const auto sourceArgument = forwardArgs.find("051001000000");
        check(forwardHeader.size() == 184 && sourceArgument != std::string::npos &&
                  forwardHeader.at(0x7A) > 0,
              "Unexpected textured pixel technique fixture");
        --forwardHeader[0x7A];
        forward["header"] = hex(forwardHeader);
        forwardArgs.erase(sourceArgument, 12);
        forward["args"] = forwardArgs;
        writeJson(techsetPath, brokenTechset);

        bool rejected = false;
        try {
            (void)replayrender::Load(renderPath.string());
        } catch (const std::runtime_error& error) {
            rejected =
                std::string(error.what()).find("native type-5 argument") != std::string::npos;
        }
        check(rejected, "Old cutout fixture did not fail its missing sourceAtlas argument");

        writeJson(techsetPath, techset);
        const auto loaded = replayrender::Load(renderPath.string());
        check(!loaded.techniques.empty(), "Corrected binding fixture did not load");

        std::error_code error;
        std::filesystem::remove_all(temporary, error);
        return 0;
    } catch (const std::exception& error) {
        std::error_code cleanupError;
        if (!temporary.empty())
            std::filesystem::remove_all(temporary, cleanupError);
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
