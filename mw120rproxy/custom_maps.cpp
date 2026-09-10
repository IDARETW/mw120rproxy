#include "door_file.h"
#include "custom_maps.h"
#include "collision_file.h"
#include "ladder_file.h"
#include "glass_file.h"

#include "logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
#include <system_error>

namespace {
std::mutex g_lock;
std::filesystem::path g_root;
std::filesystem::path g_gameRoot;
std::vector<custommaps::Package> g_packages;
std::string g_selected;

std::string ToUtf8(const std::wstring& text) {
    if (text.empty())
        return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0)
        return {};
    std::string out(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count,
                        nullptr, nullptr);
    return out;
}

bool IsMapId(const std::string& id) {
    if (id.size() < 4 || id.size() > 63 || id.rfind("mp_", 0) != 0)
        return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
    });
}

void SkipSpace(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
        ++pos;
}

bool ReadString(const std::string& text, size_t& pos, std::string& out) {
    SkipSpace(text, pos);
    if (pos >= text.size() || text[pos] != '"')
        return false;
    ++pos;
    out.clear();
    while (pos < text.size()) {
        const char ch = text[pos++];
        if (ch == '"')
            return true;
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (pos >= text.size())
            return false;
        const char escaped = text[pos++];
        switch (escaped) {
        case '"':
            out.push_back('"');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '/':
            out.push_back('/');
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        default:
            return false;
        }
    }
    return false;
}

bool SkipJsonValue(const std::string& text, size_t& pos, unsigned int depth) {
    if (depth > 16)
        return false;
    SkipSpace(text, pos);
    if (pos >= text.size())
        return false;

    if (text[pos] == '"') {
        std::string ignored;
        return ReadString(text, pos, ignored);
    }
    if (text[pos] == '[') {
        ++pos;
        SkipSpace(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            return true;
        }
        for (;;) {
            if (!SkipJsonValue(text, pos, depth + 1))
                return false;
            SkipSpace(text, pos);
            if (pos >= text.size())
                return false;
            if (text[pos] == ']') {
                ++pos;
                return true;
            }
            if (text[pos++] != ',')
                return false;
        }
    }
    if (text[pos] == '{') {
        ++pos;
        SkipSpace(text, pos);
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        for (;;) {
            std::string key;
            if (!ReadString(text, pos, key))
                return false;
            SkipSpace(text, pos);
            if (pos >= text.size() || text[pos++] != ':')
                return false;
            if (!SkipJsonValue(text, pos, depth + 1))
                return false;
            SkipSpace(text, pos);
            if (pos >= text.size())
                return false;
            if (text[pos] == '}') {
                ++pos;
                return true;
            }
            if (text[pos++] != ',')
                return false;
            SkipSpace(text, pos);
        }
    }

    const size_t start = pos;
    while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos])) &&
           text[pos] != ',' && text[pos] != ']' && text[pos] != '}')
        ++pos;
    return pos > start;
}

bool IsJsonDocument(const std::string& text) {
    size_t pos = 0;
    if (!SkipJsonValue(text, pos, 0))
        return false;
    SkipSpace(text, pos);
    return pos == text.size();
}

bool KeyPosition(const std::string& text, std::string_view key, size_t& valuePos) {
    size_t pos = 0;
    SkipSpace(text, pos);
    if (pos >= text.size() || text[pos++] != '{')
        return false;
    bool found = false;
    for (;;) {
        SkipSpace(text, pos);
        if (pos >= text.size() || text[pos] == '}')
            return found;
        std::string name;
        if (!ReadString(text, pos, name))
            return false;
        SkipSpace(text, pos);
        if (pos >= text.size() || text[pos++] != ':')
            return false;
        if (name == key) {
            if (found)
                return false;
            found = true;
            valuePos = pos;
        }
        if (!SkipJsonValue(text, pos, 0))
            return false;
        SkipSpace(text, pos);
        if (pos < text.size() && text[pos] == '}')
            return found;
        if (pos >= text.size() || text[pos++] != ',')
            return false;
    }
}

bool RequiredString(const std::string& text, std::string_view key, std::string& out) {
    size_t pos = 0;
    return KeyPosition(text, key, pos) && ReadString(text, pos, out);
}

bool OptionalString(const std::string& text, std::string_view key, std::string& out) {
    size_t pos = 0;
    if (!KeyPosition(text, key, pos))
        return true;
    return ReadString(text, pos, out);
}

bool SchemaOne(const std::string& text) {
    size_t pos = 0;
    if (!KeyPosition(text, "schema", pos))
        return false;
    SkipSpace(text, pos);
    if (pos >= text.size() || text[pos] != '1')
        return false;
    ++pos;
    SkipSpace(text, pos);
    return pos < text.size() && (text[pos] == ',' || text[pos] == '}');
}

bool HasTdm(const std::string& text) {
    size_t pos = 0;
    if (!KeyPosition(text, "gametypes", pos))
        return false;
    SkipSpace(text, pos);
    if (pos >= text.size() || text[pos++] != '[')
        return false;

    for (;;) {
        SkipSpace(text, pos);
        if (pos >= text.size())
            return false;
        if (text[pos] == ']')
            return false;
        std::string value;
        if (!ReadString(text, pos, value))
            return false;
        if (value == "tdm")
            return true;
        SkipSpace(text, pos);
        if (pos >= text.size() || text[pos] != ',')
            return false;
        ++pos;
    }
}

bool ReadText(const std::filesystem::path& path, std::string& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 2 || size > 64 * 1024)
        return false;
    input.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    input.read(out.data(), size);
    return input.good();
}

bool HasReplayFastfileHeader(const std::filesystem::path& path) {
    std::error_code error;
    const uintmax_t fileSize = std::filesystem::file_size(path, error);
    if (error || fileSize < 0x8C || fileSize > UINT32_MAX)
        return false;
    std::ifstream input(path, std::ios::binary);
    std::array<uint8_t, 0xA0> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()),
                    std::min<uintmax_t>(header.size(), fileSize)))
        return false;
    const bool stored = std::memcmp(header.data(), "IWffc100", 8) == 0;
    if (!stored && std::memcmp(header.data(), "IWffa100", 8) != 0)
        return false;
    uint32_t headerVersion = 0;
    uint32_t xfileVersion = 0;
    uint8_t compression = 0;
    uint8_t encryption = 0;
    uint32_t residentPartSize = 0;
    uint64_t xfileSize = 0;
    uint64_t streamBytes = 0;
    std::memcpy(&headerVersion, header.data() + 8, sizeof(headerVersion));
    std::memcpy(&xfileVersion, header.data() + 12, sizeof(xfileVersion));
    std::memcpy(&compression, header.data() + 16, sizeof(compression));
    std::memcpy(&encryption, header.data() + 17, sizeof(encryption));
    std::memcpy(&residentPartSize, header.data() + 0x14, sizeof(residentPartSize));
    std::memcpy(&xfileSize, header.data() + 0x20, sizeof(xfileSize));
    for (size_t index = 0; index < 11; ++index) {
        uint32_t blockSize = 0;
        std::memcpy(&blockSize, header.data() + 0x30 + index * sizeof(blockSize),
                    sizeof(blockSize));
        streamBytes += blockSize;
    }
    const bool body = stored ? compression == 0 &&
                                   std::memcmp(header.data() + 0x88, "\x01IWC", 4) == 0 &&
                                   residentPartSize == xfileSize + 4
                             : fileSize >= 0xA0 && compression == 1 &&
                                   std::memcmp(header.data() + 0x88, "IWffs100", 8) == 0;
    return headerVersion == 11 && xfileVersion == 0xFF7 && encryption == 0 && body &&
           residentPartSize == fileSize - 0x88 && xfileSize != 0 && streamBytes >= xfileSize;
}

std::array<std::string, 5> ZoneNames(const std::string& id) {
    return {id, "srv_" + id, "eng_" + id, "ww_" + id, "techsets_" + id};
}

bool ReadFastfileMetadata(custommaps::Package& package,
                          const std::filesystem::path& directory,
                          bool& present) {
    const auto path = directory / "map.json";
    present = std::filesystem::exists(path);
    if (!present)
        return true;
    if ((GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        package.error = "map.json must not be a link";
        return false;
    }

    std::string text;
    if (!ReadText(path, text) || !IsJsonDocument(text)) {
        package.error = "map.json has invalid JSON";
        return false;
    }

    std::string id;
    std::string title;
    std::string description;
    if (!OptionalString(text, "id", id) || !OptionalString(text, "title", title) ||
        !OptionalString(text, "description", description)) {
        package.error = "map.json metadata must use strings";
        return false;
    }
    if (!id.empty() && id != package.id) {
        package.error = "map.json id must match the map folder";
        return false;
    }
    if (!title.empty())
        package.title = std::move(title);
    package.description = std::move(description);
    return true;
}

bool HasStockShaderHeader(const std::filesystem::path& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ||
        (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return false;
    std::ifstream file(path, std::ios::binary);
    std::array<uint8_t, 20> header{};
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size()))
        return false;
    const uint8_t expected[]{'I', 'W', 'f',  'f',  'a', '1', '0', '0', 11, 0,
                             0,   0,   0xF7, 0x0F, 0,   0,   1,   0,   0,  0};
    return std::memcmp(header.data(), expected, sizeof(expected)) == 0;
}

std::filesystem::path ZonePath(const custommaps::Package& package,
                               const std::string& zone,
                               const std::string& extension = ".ff") {
    if (!package.shaderSource.empty() && zone == "techsets_" + package.id)
        return g_gameRoot / "zone" / ("techsets_" + package.shaderSource + extension);
    return std::filesystem::path(package.directory) / (zone + extension);
}

bool ZoneFromRequest(const char* request, std::string& zoneOut) {
    if (!request || !*request)
        return false;
    std::filesystem::path path(request);
    std::string name = path.filename().string();
    if (name.size() > 3 && name.ends_with(".ff"))
        name.resize(name.size() - 3);
    if (name.empty() || (name != request && path.extension() != ".ff"))
        return false;
    zoneOut = std::move(name);
    return true;
}

bool ValidateFastfileOnlyPackage(custommaps::Package& package,
                                 const std::filesystem::path& directory) {
    if (!IsMapId(package.id)) {
        package.error = "map folder must use the mp_ map form";
        return false;
    }
    if ((GetFileAttributesW(directory.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        package.error = "map folders must not be links";
        return false;
    }
    if (std::filesystem::exists(g_gameRoot / "zone" / (package.id + ".ff")) ||
        std::filesystem::exists(g_gameRoot / (package.id + ".ff"))) {
        package.error = "package id conflicts with an installed stock map";
        return false;
    }

    bool hasMetadata = false;
    if (!ReadFastfileMetadata(package, directory, hasMetadata))
        return false;

    std::vector<std::string> expected;
    for (const auto& zone : ZoneNames(package.id))
        expected.push_back(zone + ".ff");
    if (hasMetadata)
        expected.emplace_back("map.json");
    std::sort(expected.begin(), expected.end());

    std::vector<std::string> actual;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file(error)) {
            package.error = "fastfile-only map output contains an unreadable entry";
            return false;
        }
        actual.push_back(entry.path().filename().string());
    }
    if (error) {
        package.error = "fastfile-only map output cannot be read";
        return false;
    }
    std::sort(actual.begin(), actual.end());
    if (actual != expected) {
        package.error = "fastfile-only map output must contain five map zones and optional map.json";
        return false;
    }

    for (const auto& zone : ZoneNames(package.id)) {
        const auto fastfile = directory / (zone + ".ff");
        if ((GetFileAttributesW(fastfile.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            !HasReplayFastfileHeader(fastfile)) {
            package.error = "zone fastfile has an invalid 1.20 header";
            return false;
        }
    }
    package.visibility = "all-visible-v1";
    package.worldFormat = "replay-1.20-native-v1";
    package.valid = true;
    return true;
}

void ValidatePackage(custommaps::Package& package, const std::filesystem::path& directory) {
    package.directory = directory.wstring();
    package.title = ToUtf8(directory.filename().wstring());
    package.id = package.title;

    const std::filesystem::path manifest = directory / "manifest.json";
    std::string text;
    if (!ReadText(manifest, text)) {
        ValidateFastfileOnlyPackage(package, directory);
        return;
    }
    if (!IsJsonDocument(text)) {
        package.error = "manifest.json has invalid JSON";
        return;
    }
    if (!SchemaOne(text)) {
        package.error = "manifest schema must be 1";
        return;
    }
    if (!RequiredString(text, "id", package.id) || !IsMapId(package.id)) {
        package.error = "manifest id must use the mp_ map form";
        return;
    }
    if (package.id != ToUtf8(directory.filename().wstring())) {
        package.error = "manifest id must match the map folder";
        return;
    }
    if ((GetFileAttributesW(directory.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        package.error = "map folders must not be links";
        return;
    }
    if (std::filesystem::exists(g_gameRoot / "zone" / (package.id + ".ff")) ||
        std::filesystem::exists(g_gameRoot / (package.id + ".ff"))) {
        package.error = "package id conflicts with an installed stock map";
        return;
    }
    if (!RequiredString(text, "title", package.title) || package.title.empty()) {
        package.error = "manifest title is missing";
        return;
    }
    if (!OptionalString(text, "description", package.description)) {
        package.error = "manifest description is invalid";
        return;
    }
    if (!HasTdm(text)) {
        package.error = "Team Deathmatch support is required";
        return;
    }

    if (!OptionalString(text, "shaderSource", package.shaderSource) ||
        (!package.shaderSource.empty() && package.shaderSource != "mp_frontend3")) {
        package.error = "shaderSource must be the supported Replay mp_frontend3 dependency";
        return;
    }
    if (!package.shaderSource.empty() &&
        !HasStockShaderHeader(ZonePath(package, "techsets_" + package.id))) {
        package.error = "installed Replay shader dependency is missing or invalid";
        return;
    }
    if (!OptionalString(text, "visibility", package.visibility) ||
        (!package.visibility.empty() && package.visibility != "all-visible-v1")) {
        package.error = "unsupported visibility contract";
        return;
    }
    if (!OptionalString(text, "world", package.worldFormat) ||
        (!package.worldFormat.empty() && package.worldFormat != "replay-1.20-native-v1")) {
        package.error = "unsupported world format";
        return;
    }

    for (const std::string& zone : ZoneNames(package.id)) {
        const std::filesystem::path fastfile = directory / (zone + ".ff");
        if (!std::filesystem::is_regular_file(fastfile)) {
            package.error = "zone family is incomplete";
            return;
        }
        if ((GetFileAttributesW(fastfile.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            package.error = "package fastfiles must not be links";
            return;
        }
        if (!HasReplayFastfileHeader(fastfile)) {
            package.error = "zone fastfile has an invalid 1.20 header";
            return;
        }
    }
    std::string collision;
    if (!OptionalString(text, "collision", collision) ||
        (!collision.empty() && collision != "boxes-v1" && collision != "convex-v2" &&
         collision != "convex-v3")) {
        package.error = "unsupported collision format";
        return;
    }
    const auto collisionPath = directory / "collision.bin";
    if (!collision.empty() || std::filesystem::exists(collisionPath)) {
        std::vector<collisionfile::Brush> brushes;
        if ((GetFileAttributesW(collisionPath.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !collisionfile::Load(collisionPath, brushes)) {
            package.error = "collision.bin is missing or invalid";
            return;
        }
    }
    std::string glass;
    if (!OptionalString(text, "glass", glass) ||
        (!glass.empty() && glass != "panes-v1" && glass != "panes-v2")) {
        package.error = "unsupported glass format";
        return;
    }
    if (!glass.empty() || std::filesystem::exists(directory / "glass.bin")) {
        std::vector<glassfile::Pane> panes;
        unsigned surfaces = 0;
        if (!glassfile::Load(directory / "glass.bin", panes, surfaces)) {
            package.error = "glass.bin is missing or invalid";
            return;
        }
    }
    std::string doors;
    if (!OptionalString(text, "doors", doors) || (!doors.empty() && doors != "brush-poses-v1")) {
        package.error = "unsupported door format";
        return;
    }
    if (!doors.empty() || std::filesystem::exists(directory / "doors.bin")) {
        std::vector<doorfile::Door> data;
        unsigned surfaces = 0;
        if ((GetFileAttributesW((directory / "doors.bin").c_str()) &
             FILE_ATTRIBUTE_REPARSE_POINT) ||
            !doorfile::Load(directory / "doors.bin", data, surfaces)) {
            package.error = "doors.bin is missing or invalid";
            return;
        }
    }
    std::string ladders;
    if (!OptionalString(text, "ladders", ladders) ||
        (!ladders.empty() && ladders != "faces-v1" && ladders != "faces-v2")) {
        package.error = "unsupported ladder format";
        return;
    }
    const auto ladderPath = directory / "ladders.bin";
    if (!ladders.empty() || std::filesystem::exists(ladderPath)) {
        std::vector<ladderfile::Face> faces;
        if ((GetFileAttributesW(ladderPath.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !ladderfile::Load(ladderPath, faces)) {
            package.error = "ladders.bin is missing or invalid";
            return;
        }
    }
    package.valid = true;
}

void ScanLocked() {
    std::vector<custommaps::Package> packages;
    std::error_code error;
    if (g_root.empty() || !std::filesystem::is_directory(g_root, error)) {
        g_packages.clear();
        g_selected.clear();
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(g_root, error)) {
        if (error)
            break;
        if (!entry.is_directory(error))
            continue;
        custommaps::Package package;
        ValidatePackage(package, entry.path());
        packages.push_back(std::move(package));
    }

    std::sort(packages.begin(), packages.end(),
              [](const custommaps::Package& left, const custommaps::Package& right) {
                  return left.title < right.title;
              });

    const bool selectionStillExists =
        std::any_of(packages.begin(), packages.end(), [](const custommaps::Package& package) {
            return package.valid && package.id == g_selected;
        });
    if (!selectionStillExists)
        g_selected.clear();
    g_packages = std::move(packages);
}
}

namespace custommaps {
void Initialize(HMODULE self) {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(self, modulePath, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        LOG_WARN("Maps", "could not resolve the proxy path");
        return;
    }

    std::lock_guard<std::mutex> lock(g_lock);
    g_gameRoot = std::filesystem::path(modulePath).parent_path();
    g_root = g_gameRoot / L"mods" / L"mw120r" / L"maps";
    ScanLocked();
    LOG_INFO("Maps", "scanned %zu custom map package(s)", g_packages.size());
}

void Refresh() {
    std::lock_guard<std::mutex> lock(g_lock);
    ScanLocked();
    LOG_INFO("Maps", "refreshed %zu custom map package(s)", g_packages.size());
}

std::vector<Package> List() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_packages;
}

bool Select(const char* id) {
    if (!id)
        return false;
    std::lock_guard<std::mutex> lock(g_lock);
    const auto found =
        std::find_if(g_packages.begin(), g_packages.end(), [id](const Package& package) {
            return package.valid && package.id == id;
        });
    if (found == g_packages.end())
        return false;
    g_selected = found->id;
    LOG_INFO("Maps", "selected '%s'", id);
    return true;
}

std::string Active() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_selected;
}

void ClearSelection() {
    std::lock_guard<std::mutex> lock(g_lock);
    g_selected.clear();
}

std::string Status(const char* id) {
    if (!id)
        return "map id is missing";
    std::lock_guard<std::mutex> lock(g_lock);
    const auto found =
        std::find_if(g_packages.begin(), g_packages.end(), [id](const Package& package) {
            return package.id == id;
        });
    if (found == g_packages.end())
        return "map package was not found";
    return found->valid ? "structural checks passed; engine load unverified" : found->error;
}

bool ActiveZonePath(const char* zoneName, std::wstring& pathOut) {
    if (!zoneName)
        return false;
    std::lock_guard<std::mutex> lock(g_lock);
    const auto package =
        std::find_if(g_packages.begin(), g_packages.end(), [](const Package& item) {
            return item.valid && item.id == g_selected;
        });
    if (package == g_packages.end())
        return false;
    for (const std::string& zone : ZoneNames(package->id)) {
        if (zone == zoneName) {
            pathOut = ZonePath(*package, zone).wstring();
            return true;
        }
    }
    return false;
}

bool ActiveShaderSource(const char* zoneName, std::string& sourceOut) {
    sourceOut.clear();
    if (!zoneName)
        return false;
    std::lock_guard<std::mutex> lock(g_lock);
    for (const auto& package : g_packages)
        if (package.valid && package.id == g_selected && !package.shaderSource.empty() &&
            std::string("techsets_") + package.id == zoneName) {
            sourceOut = "techsets_" + package.shaderSource + ".ff";
            return true;
        }
    return false;
}

bool ActiveZoneQPath(const char* request, char* qpathOut, size_t qpathOutSize) {
    if (!qpathOut || qpathOutSize == 0)
        return false;
    qpathOut[0] = 0;

    std::string zone;
    if (!ZoneFromRequest(request, zone))
        return false;

    std::lock_guard<std::mutex> lock(g_lock);
    const auto package =
        std::find_if(g_packages.begin(), g_packages.end(), [](const Package& item) {
            return item.valid && item.id == g_selected;
        });
    if (package == g_packages.end())
        return false;
    const auto names = ZoneNames(package->id);
    if (std::find(names.begin(), names.end(), zone) == names.end())
        return false;

    const auto relative = ZonePath(*package, zone).lexically_relative(g_gameRoot).generic_string();
    const int written = std::snprintf(qpathOut, qpathOutSize, "%s", relative.c_str());
    return written > 0 && static_cast<size_t>(written) < qpathOutSize;
}

bool IsKnownMap(const char* mapName) {
    if (!mapName)
        return false;
    std::lock_guard<std::mutex> lock(g_lock);
    return std::any_of(g_packages.begin(), g_packages.end(), [mapName](const Package& package) {
        return package.valid && package.id == mapName;
    });
}

bool ActiveWorldContract() {
    std::lock_guard<std::mutex> lock(g_lock);
    const auto package =
        std::find_if(g_packages.begin(), g_packages.end(), [](const Package& item) {
            return item.valid && item.id == g_selected;
        });
    if (package == g_packages.end())
        return false;
    return (package->visibility.empty() || package->visibility == "all-visible-v1") &&
           (package->worldFormat.empty() || package->worldFormat == "replay-1.20-native-v1");
}

bool ResolveDiskRead(const char* request, std::string& pathOut) {
    pathOut.clear();
    if (!request || !*request)
        return false;
    const auto source =
        std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(request)));
    for (const auto& component : source)
        if (component == "..")
            return false;
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_selected.empty())
        return false;
    const auto package =
        std::find_if(g_packages.begin(), g_packages.end(), [](const Package& item) {
            return item.valid && item.id == g_selected;
        });
    if (package == g_packages.end())
        return false;
    const auto full = (source.is_absolute() ? source : g_gameRoot / source).lexically_normal();
    for (const auto& zone : ZoneNames(package->id)) {
        const auto extension = source.extension().string();
        const bool shader = !package->shaderSource.empty() && zone == "techsets_" + package->id;
        if (extension != ".ff" && !(shader && extension == ".xpak"))
            continue;
        const auto filename = zone + extension;
        for (const auto& folder :
             {std::filesystem::path{}, std::filesystem::path{"zone"},
              std::filesystem::path{"zone/english"}, std::filesystem::path{"zone/worldwide"}}) {
            const auto expected = (g_gameRoot / folder / filename).lexically_normal();
            if (_wcsicmp(full.c_str(), expected.c_str()) != 0)
                continue;
            const auto target = ZonePath(*package, zone, extension).lexically_normal();
            // Recheck links at the handoff; the files remain owned by the native reader.
            if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES ||
                (GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                (GetFileAttributesW(package->directory.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) !=
                    0)
                return false;
            pathOut = ToUtf8(target.wstring());
            return !pathOut.empty();
        }
    }
    return false;
}
}
