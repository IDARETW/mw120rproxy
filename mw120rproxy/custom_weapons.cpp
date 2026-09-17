#include "custom_weapons.h"
#include "game.h"
#include "logger.h"
#include "safemem.h"
#include "../iw8-zonetool/src/common/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace {
struct Package {
    std::string base, common, asset, title, reference, titleKey, descriptionKey;
    std::map<std::string, std::string> zonePaths;
    unsigned slot{};
    bool attachments{};
    std::string defaultAttachments;
    std::map<std::string, std::string> attachmentMap;
    struct AttachmentRow { std::string asset, reference, token, title; };
    std::vector<AttachmentRow> attachmentRows;
};
std::vector<std::unique_ptr<Package>> g_packages;
std::atomic<bool> g_available{false};
std::mutex g_mutex;
struct ZoneInfo {
    const char* name;
    uint32_t flags, failureMode, priority, padding;
};
static_assert(sizeof(ZoneInfo) == 24);
using Load = uintptr_t (*)(const ZoneInfo*, uint32_t, uint32_t, bool);
std::atomic<Load> g_load{nullptr};

bool ReadArray(const void* source, void* dest, size_t size) {
    auto* in = static_cast<const unsigned char*>(source);
    auto* out = static_cast<unsigned char*>(dest);
    while (size) {
        size_t available = 0;
        if (!safemem::RegionReadable(in, available) || !available)
            return false;
        const size_t n = (std::min)(size, available);
        memcpy(out, in, n);
        in += n;
        out += n;
        size -= n;
    }
    return true;
}
std::string String(const char* pointer) {
    char text[4096]{};
    if (!pointer || safemem::ReadString(pointer, text, sizeof(text)) >= sizeof(text) - 1)
        throw std::runtime_error("unreadable weapon registration string");
    return text;
}
uint32_t Hash(const std::string& text, bool lower = true) {
    uint32_t hash = 0;
    for (unsigned char c : text)
        hash = hash * 31 + (lower && c >= 'A' && c <= 'Z' ? c + 32 : c);
    return hash;
}
struct Table {
    const char* name;
    int columns, rows, unique, padding;
    uint16_t* cells;
    uint32_t* hashes;
    const char** strings;
};
static_assert(sizeof(Table) == 48);
struct TableCopy {
    const void* source{};
    const void* sourceCells{};
    Table table{};
    std::vector<uint16_t> cells;
    std::vector<uint32_t> hashes;
    std::vector<const char*> pointers;
    std::deque<std::string> strings;
    uint16_t Intern(const std::string& value) {
        for (size_t i = 0; i < pointers.size(); ++i)
            if (value == pointers[i])
                return static_cast<uint16_t>(i);
        if (pointers.size() >= 65535)
            throw std::runtime_error("weapon table string dictionary is full");
        strings.push_back(value);
        pointers.push_back(strings.back().c_str());
        hashes.push_back(Hash(value));
        return static_cast<uint16_t>(pointers.size() - 1);
    }
};
std::vector<std::unique_ptr<TableCopy>> g_tables;

void* ExtendTable(const char* name, void* source) {
    const bool stats = !_stricmp(name, "mp/statsTable.csv");
    const bool mapping = !_stricmp(name, "mp/attachmentmap.csv");
    const bool attachments = !_stricmp(name, "mp/attachmenttable.csv");
    if (!stats && !mapping && !attachments && _stricmp(name, "loot/weapon_ids.csv"))
        return source;
    // DB_FindXAssetHeader has just returned this live, typed asset. Native table
    // consumers read the same header directly. VirtualQuery here made every
    // cache hit pay a kernel memory-map query (15 ms in Replay's menu trace).
    // Still compare the live cell pointer so a reloaded zone gets a new copy.
    const Table original = *static_cast<const Table*>(source);
    if (original.columns != (stats ? 61 : mapping ? 163 : attachments ? 33 : 7) || original.rows < 1 || original.rows > 10000 ||
        original.unique < 1 || original.unique > 65000)
        throw std::runtime_error("unexpected Replay weapon table layout");
    for (const auto& cached : g_tables)
        if (cached->source == source && cached->sourceCells == original.cells)
            return &cached->table;
    auto copy = std::make_unique<TableCopy>();
    copy->source = source;
    copy->sourceCells = original.cells;
    copy->table = original;
    copy->cells.resize(size_t(original.columns) * original.rows);
    copy->hashes.resize(original.unique);
    copy->pointers.resize(original.unique);
    if (!ReadArray(original.cells, copy->cells.data(), copy->cells.size() * 2) ||
        !ReadArray(original.hashes, copy->hashes.data(), copy->hashes.size() * 4) ||
        !ReadArray(original.strings, copy->pointers.data(), copy->pointers.size() * 8))
        throw std::runtime_error("unreadable weapon table arrays");
    for (auto cell : copy->cells)
        if (cell >= original.unique)
            throw std::runtime_error("invalid weapon table cell");
    for (size_t i = 0; i < copy->pointers.size(); ++i) {
        const auto value = String(copy->pointers[i]);
        if (Hash(value) != copy->hashes[i])
            throw std::runtime_error("weapon table string hash mismatch");
        copy->strings.push_back(value);
        copy->pointers[i] = copy->strings.back().c_str();
    }
    if (mapping) {
        // Shared perks normally resolve directly by token and need no stock map
        // column. An owned replacement needs a per-weapon mapping for that same
        // saved token. The native StringTable format supports additional columns.
        std::set<std::string> existing;
        for (int column = 1; column < original.columns; ++column)
            existing.emplace(copy->pointers[copy->cells[column]]);
        std::set<std::string> extra;
        for (const auto& package : g_packages)
            for (const auto& [token, asset] : package->attachmentMap)
                if (!existing.contains(token))
                    extra.insert(token);
        const int columns = original.columns + int(extra.size());
        if (columns > 256)
            throw std::runtime_error("custom attachment map exceeds 256 columns");
        if (!extra.empty()) {
            std::vector<uint16_t> cells(size_t(original.rows) * columns, copy->Intern(""));
            for (int row = 0; row < original.rows; ++row)
                std::copy_n(copy->cells.begin() + row * original.columns, original.columns,
                            cells.begin() + row * columns);
            int column = original.columns;
            for (const auto& token : extra)
                cells[column++] = copy->Intern(token);
            copy->cells = std::move(cells);
            copy->table.columns = columns;
        }
    }
    const int refColumn = stats ? 4 : 1;
    if (stats) {
        // Replay's GSC weapon-data initializer looks up IDs 1, 2, ... and
        // stops at the first missing ID, rather than iterating table rows.
        std::set<std::string> ids;
        for (int row = 0; row < original.rows; ++row)
            ids.emplace(copy->pointers[copy->cells[row * original.columns]]);
        for (int id = 1; id <= original.rows; ++id)
            if (!ids.contains(std::to_string(id)))
                throw std::runtime_error("non-contiguous stock weapon stat IDs");
    }
    for (const auto& package : g_packages) {
        if (mapping) {
            std::vector<uint16_t> row(copy->table.columns, copy->Intern(""));
            row[0] = copy->Intern(package->base);
            for (int column = 1; column < copy->table.columns; ++column) {
                const auto token = copy->pointers[copy->cells[column]];
                if (const auto found = package->attachmentMap.find(token); found != package->attachmentMap.end())
                    row[column] = copy->Intern(found->second);
            }
            copy->cells.insert(copy->cells.end(), row.begin(), row.end());
            ++copy->table.rows;
            continue;
        }
        if (attachments) {
            for (const auto& attachment : package->attachmentRows) {
                int referenceRow = -1;
                for (int row = 0; row < original.rows; ++row)
                    if (attachment.reference == copy->pointers[copy->cells[row * original.columns + 4]]) {
                        referenceRow = row;
                        break;
                    }
                if (referenceRow < 0)
                    throw std::runtime_error("attachment table lacks source: " + attachment.reference);
                std::vector<uint16_t> row(copy->cells.begin() + referenceRow * original.columns,
                                          copy->cells.begin() + (referenceRow + 1) * original.columns);
                row[0] = copy->Intern(std::to_string(copy->table.rows));
                row[3] = copy->Intern("CUSTOM_ATTACHMENT/" + attachment.asset);
                row[4] = copy->Intern(attachment.asset);
                row[5] = copy->Intern(attachment.token);
                copy->cells.insert(copy->cells.end(), row.begin(), row.end());
                ++copy->table.rows;
            }
            continue;
        }
        const auto reference = package->reference.substr(0, package->reference.size() - 3);
        int referenceRow = -1;
        for (int row = 0; row < original.rows; ++row) {
            const char* ref = copy->pointers[copy->cells[row * original.columns + refColumn]];
            if (package->base == ref)
                throw std::runtime_error("custom weapon would replace an existing table row");
            if (reference == ref && referenceRow < 0)
                referenceRow = row;
        }
        if (referenceRow < 0)
            throw std::runtime_error("weapon table lacks loadout reference");
        const size_t begin = copy->cells.size();
        std::vector<uint16_t> row(copy->cells.begin() + referenceRow * original.columns,
                                  copy->cells.begin() + (referenceRow + 1) * original.columns);
        copy->cells.insert(copy->cells.end(), row.begin(), row.end());
        const auto set = [&](int column, const std::string& value) {
            copy->cells[begin + column] = copy->Intern(value);
        };
        // Stat IDs must continue the native script's contiguous range. Loot
        // IDs and saved DDL ordinals are separate namespaces and stay stable.
        set(0, std::to_string(stats ? copy->table.rows + 1 : 60000 + package->slot));
        set(refColumn, package->base);
        if (stats) {
            set(3, package->titleKey);
            set(5, package->asset);
            set(7, package->descriptionKey);
            set(9, package->defaultAttachments);
            if (!package->attachments) {
                set(11, "0");
                set(12, "");
            }
            set(13, "");
            set(14, "");
            set(15, "");
            set(41, std::to_string(1000 + package->slot));
            set(42, "1");
            set(48, "0"); // no cosmetic attachments
            set(49, "");
            set(51, "");
            if (!package->attachments) {
                set(52, "1");
                set(53, "1");
                set(54, "0");
            }
        } else {
            set(2, "0");
            set(3, "0");
            set(4, "0");
            set(5, "0");
            set(6, package->base + "_variant_0");
        }
        ++copy->table.rows;
    }
    copy->table.cells = copy->cells.data();
    copy->table.hashes = copy->hashes.data();
    copy->table.strings = copy->pointers.data();
    copy->table.unique = static_cast<int>(copy->pointers.size());
    auto* result = &copy->table;
    logger::Trace("Weapons", "table '%s': preserved %d rows, appended %zu base weapons", name,
                  original.rows, g_packages.size());
    g_tables.push_back(std::move(copy));
    return result;
}

struct DDLHash {
    uint32_t hash;
    int index;
};
struct DDLHashTable {
    DDLHash* list;
    int count, max;
};
struct DDLEnum {
    const char* name;
    int count, padding;
    const char** members;
    DDLHashTable hash;
};
struct DDLMember {
    const char* name;
    int index, unknown;
    const uint8_t* serializedByte;
    int bitSize, limitSize, offset, type, externalIndex;
    uint32_t rangeLimit;
    bool isArray;
    uint8_t padding[3];
    int arraySize, enumIndex, unknownTail;
};
struct DDLStruct {
    const char* name;
    int bitSize, count;
    DDLMember* members;
    DDLHashTable upper, lower;
};
struct DDLDef {
    const char* name;
    uint16_t version;
    uint8_t padding[6];
    uint64_t guidSeed, guid;
    int bitSize, byteSize;
    DDLStruct* structs;
    int structCount, padding2;
    DDLEnum* enums;
    int enumCount, padding3;
    DDLDef* next;
    int headerBitSize, headerByteSize;
    bool paddingUsed, minimalHeader;
    uint8_t tail[6];
};
struct DDLFile {
    const char* name;
    DDLDef* definition;
};
static_assert(sizeof(DDLDef) == 96 && sizeof(DDLEnum) == 40 && sizeof(DDLMember) == 64 &&
              sizeof(DDLStruct) == 56);
struct DDLCopy {
    void* source{};
    DDLDef* sourceDef{};
    DDLFile file{};
    DDLDef definition{};
    std::vector<DDLEnum> enums;
    std::vector<DDLStruct> structs;
    std::vector<std::vector<DDLMember>> structMembers;
    std::deque<std::string> strings;
    std::vector<const char*> members;
    std::vector<DDLHash> hashes;
};
std::vector<std::unique_ptr<DDLCopy>> g_ddls;

void* ExtendDDL(const char* name, void* source) {
    const bool recipe = !_stricmp(name, "ddl/mp/recipes.ddl");
    if (_stricmp(name, "ddl/mp/playerdata.ddl") && _stricmp(name, "ddl/mp/privateloadouts.ddl") &&
        _stricmp(name, "ddl/mp/rankedloadouts.ddl") && !recipe)
        return source;
    const DDLFile file = *static_cast<const DDLFile*>(source);
    for (const auto& cached : g_ddls)
        if (cached->source == source && cached->sourceDef == file.definition)
            return &cached->file;
    DDLDef definition{};
    if (!safemem::ReadBytes(file.definition, &definition, sizeof(definition)) ||
        definition.enumCount < 1 || definition.enumCount > 256 || definition.structCount < 1 ||
        definition.structCount > 256)
        throw std::runtime_error("invalid loadout schema header");
    auto copy = std::make_unique<DDLCopy>();
    copy->source = source;
    copy->sourceDef = file.definition;
    copy->file = file;
    copy->definition = definition;
    copy->enums.resize(definition.enumCount);
    if (!ReadArray(definition.enums, copy->enums.data(), copy->enums.size() * sizeof(DDLEnum)))
        throw std::runtime_error("unreadable loadout enums");
    int enumIndex = -1;
    for (int i = 0; i < definition.enumCount; ++i)
        if (String(copy->enums[i].name) == "LoadoutWeapon")
            enumIndex = i;
    if (enumIndex < 0 || copy->enums[enumIndex].count != 61)
        throw std::runtime_error("unsupported LoadoutWeapon enum; expected 61 stock entries");
    auto target = copy->enums[enumIndex];
    // Verify every consumer fits the new values without resizing a bit field or
    // a saved array. The historical DDL chain, GUID and all offsets stay intact.
    copy->structs.resize(definition.structCount);
    copy->structMembers.resize(definition.structCount);
    if (!ReadArray(definition.structs, copy->structs.data(),
                   copy->structs.size() * sizeof(DDLStruct)))
        throw std::runtime_error("unreadable loadout structs");
    unsigned consumers = 0;
    for (size_t i = 0; i < copy->structs.size(); ++i) {
        auto& record = copy->structs[i];
        if (record.count < 0 || record.count > 4096)
            throw std::runtime_error("invalid loadout member count");
        auto& members = copy->structMembers[i];
        members.resize(record.count);
        if (!ReadArray(record.members, members.data(), members.size() * sizeof(DDLMember)))
            throw std::runtime_error("unreadable loadout members");
        bool changed = false;
        for (auto& member : members) {
            if (member.type == 10 && member.externalIndex == enumIndex) {
                if (member.isArray || member.bitSize != 8)
                    throw std::runtime_error("LoadoutWeapon consumer is not an 8-bit scalar");
                member.externalIndex = definition.enumCount;
                changed = true;
                ++consumers;
            }
        }
        if (changed)
            record.members = members.data();
    }
    if (!consumers)
        throw std::runtime_error("no verified LoadoutWeapon consumers");
    std::vector<const char*> stock(target.count);
    if (!ReadArray(target.members, stock.data(), stock.size() * sizeof(const char*)))
        throw std::runtime_error("unreadable loadout weapon names");
    for (auto* value : stock)
        copy->strings.push_back(String(value));
    // Recipes reserve the full byte-sized ID space so adding another installed
    // weapon never changes the persisted recipe layout again.
    unsigned count = recipe ? 256 : 61;
    for (const auto& package : g_packages)
        count = (std::max)(count, package->slot + 1);
    for (unsigned i = 61; i < count; ++i) {
        auto it = std::find_if(g_packages.begin(), g_packages.end(), [i](const auto& p) {
            return p->slot == i;
        });
        copy->strings.push_back(it == g_packages.end() ? "cw_reserved_" + std::to_string(i)
                                                       : (*it)->base);
    }
    for (const auto& value : copy->strings) {
        copy->hashes.push_back({Hash(value, false), static_cast<int>(copy->members.size())});
        copy->members.push_back(value.c_str());
    }
    std::sort(copy->hashes.begin(), copy->hashes.end(), [](auto a, auto b) {
        return a.hash < b.hash;
    });
    for (size_t i = 1; i < copy->hashes.size(); ++i)
        if (copy->hashes[i - 1].hash == copy->hashes[i].hash)
            throw std::runtime_error("custom LoadoutWeapon hash collision");
    target.count = static_cast<int>(count);
    target.name = "CustomLoadoutWeapon";
    target.members = copy->members.data();
    target.hash = {copy->hashes.data(), static_cast<int>(count), static_cast<int>(count)};
    // The original enum also indexes SquadMember.weapon_xp[61]. Extending it
    // would change the meaning of that fixed saved array. Give only the 8-bit
    // weapon-selection fields a new enum; keep all stock progression data exact.
    copy->enums.push_back(target);
    copy->definition.enumCount = static_cast<int>(copy->enums.size());
    copy->definition.enums = copy->enums.data();
    copy->definition.structs = copy->structs.data();
    if (recipe) {
        if (definition.version != 463 || definition.byteSize != 3999 ||
            definition.headerBitSize != 176 || definition.structCount != 61 ||
            String(copy->structs[0].name) != "root" ||
            String(copy->structs[29].name) != "CommonOptions" || copy->structs[29].bitSize != 2048)
            throw std::runtime_error("unsupported Replay recipe layout");
        // CommonOptions.weaponRestricted is a 61-bit enum-indexed array at
        // bit 1776. Grow it to 256 bits, move the following fields by 195 bits,
        // then round the enclosing struct to 2248 bits (25 additional bytes).
        auto& common = copy->structs[29];
        auto& members = copy->structMembers[29];
        bool restriction = false, padding = false;
        for (auto& member : members) {
            const auto memberName = String(member.name);
            if (memberName == "weaponRestricted") {
                if (member.offset != 1776 || member.bitSize != 61 || member.type != 2 ||
                    !member.isArray || member.arraySize != 61 || member.enumIndex != enumIndex)
                    throw std::runtime_error("unexpected recipe weapon restriction array");
                member.bitSize = member.arraySize = 256;
                member.enumIndex = definition.enumCount;
                restriction = true;
            } else if (memberName == "__pad") {
                if (member.offset != 2046 || member.bitSize != 2 || member.type != 11)
                    throw std::runtime_error("unexpected recipe padding");
                member.offset = 2241;
                member.bitSize = member.limitSize = 7;
                padding = true;
            } else if (member.offset >= 1837) {
                member.offset += 195;
            } else if (member.offset + member.bitSize > 1776) {
                throw std::runtime_error("recipe field overlaps weapon restriction array");
            }
        }
        if (!restriction || !padding)
            throw std::runtime_error("recipe restriction layout is incomplete");
        common.bitSize = 2248;
        common.members = members.data();
        unsigned parents = 0;
        for (size_t i = 0; i < copy->structs.size(); ++i) {
            for (auto& member : copy->structMembers[i]) {
                if (member.type == 9 && member.externalIndex == 29) {
                    if (i != 0 || member.isArray || member.offset != 0 || member.bitSize != 2048)
                        throw std::runtime_error("unexpected CommonOptions parent");
                    member.bitSize = 2248;
                    ++parents;
                } else if (i == 0) {
                    if (member.offset < 2048)
                        throw std::runtime_error("root field overlaps CommonOptions");
                    member.offset += 200;
                }
            }
        }
        if (parents != 1)
            throw std::runtime_error("recipe must have one CommonOptions parent");
        copy->structs[0].members = copy->structMembers[0].data();
        copy->structs[0].bitSize += 200;
        copy->definition.bitSize += 200;
        copy->definition.byteSize += 25;
        // DDL_Buffer_CreateContext (Replay 2053DB0) converts old buffers by
        // version through this original definition chain. Never reinterpret
        // version 463 bytes using the new offsets. 4024 bytes fits its 4096 buffer.
        copy->definition.version = 0x8000 | 463;
        copy->definition.guid ^= 0x43575F5243503031ULL; // custom recipe layout v1
        copy->definition.next = file.definition;
    }
    copy->file.definition = &copy->definition;
    auto* result = &copy->file;
    if (recipe)
        logger::Trace(
            "Weapons",
            "DDL '%s': native recipe conversion v463 -> v%u, %d bytes, 256 restriction slots", name,
            copy->definition.version, copy->definition.byteSize);
    else
        logger::Trace(
            "Weapons",
            "DDL '%s' v%u: retained stock slots 0..60 and bit layout; custom slots through %u",
            name, definition.version, count - 1);
    g_ddls.push_back(std::move(copy));
    return result;
}

uintptr_t LoadWithWeapons(const ZoneInfo* zones, uint32_t count, uint32_t mode, bool parameter) {
    const auto original = g_load.load(std::memory_order_acquire);
    if (!g_available.load(std::memory_order_acquire) || !count || count > 256)
        return original(zones, count, mode, parameter);
    const DWORD saved = GetLastError();
    std::vector<ZoneInfo> requests;
    try {
        requests.resize(count);
        if (!ReadArray(zones, requests.data(), requests.size() * sizeof(ZoneInfo)))
            throw std::runtime_error("unreadable zone requests");
        std::map<std::string, ZoneInfo> owners;
        std::set<std::string> requested;
        for (const auto& zone : requests) {
            const auto name = String(zone.name);
            requested.insert(name);
            if (name == "global_stream_mp" || name == "common_mp")
                owners.emplace(name, zone);
        }
        if (!owners.empty()) {
            for (const auto& [ownerName, source] : owners)
                for (const auto& package : g_packages) {
                    const auto& zone = ownerName == "common_mp" ? package->common : package->base;
                    if (requested.insert(zone).second) {
                        auto owner = source;
                        owner.name = zone.c_str();
                        requests.push_back(owner);
                        LOG_INFO("Weapons", "queued '%s' with '%s' flags=0x%X priority=%u",
                                 owner.name, ownerName.c_str(), owner.flags, owner.priority);
                    }
                }
            SetLastError(saved);
            return original(requests.data(), static_cast<uint32_t>(requests.size()), mode,
                            parameter);
        }
    } catch (const std::exception& error) {
        logger::Trace("Weapons", "zone registration failed: %s", error.what());
    }
    SetLastError(saved);
    return original(zones, count, mode, parameter);
}
}

namespace customweapons {
hook::Status Install(uintptr_t base) {
    if (g_load.load(std::memory_order_acquire))
        return hook::Status::Installed;
    wchar_t executable[32768]{};
    const DWORD length =
        GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable))
        return hook::Status::Failed;
    const auto root = std::filesystem::path(executable).parent_path() / "zone";
    std::vector<std::unique_ptr<Package>> packages;
    std::set<unsigned> slots;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            const auto filename = entry.path().filename().string();
            if (!filename.starts_with("iw8_cw_") || !filename.ends_with(".weapon.json"))
                continue;
            if (!entry.is_regular_file() || entry.file_size() > 131072)
                throw std::runtime_error("invalid custom weapon metadata file");
            std::ifstream input(entry.path());
            const auto metadata = nlohmann::json::parse(input);
            auto package = std::make_unique<Package>();
            package->base = metadata.at("base");
            package->common = package->base + "_common";
            package->asset = metadata.at("asset");
            package->title = metadata.at("display_name");
            package->reference = metadata.at("reference");
            package->slot = metadata.at("loadout_slot").get<unsigned>();
            package->attachments = metadata.at("attachments").get<bool>();
            const bool project = metadata.at("format") == "replay-custom-weapon-v3";
            if ((!project && metadata.at("format") != "replay-custom-weapon-v2") ||
                (!project && package->attachments) || package->base.size() < 8 ||
                package->base.size() > 48 || !package->base.starts_with("iw8_cw_") ||
                package->base.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") !=
                    std::string::npos ||
                filename != package->base + ".weapon.json" ||
                package->asset != package->base + "_mp" || package->title.empty() ||
                package->title.size() > 128 || package->title.find('\0') != std::string::npos ||
                !package->reference.starts_with("iw8_") ||
                package->reference.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
                !package->reference.ends_with("_mp") || package->slot < 61 || package->slot > 254 ||
                !slots.insert(package->slot).second)
                throw std::runtime_error("invalid or conflicting custom weapon registration");
            if (project) {
                package->defaultAttachments = metadata.value("default_attachments", std::string());
                package->attachmentMap = metadata.value("attachment_map", std::map<std::string, std::string>());
                if (package->defaultAttachments.size() > 1024 || package->attachmentMap.size() > 162)
                    throw std::runtime_error("oversized weapon attachment mapping");
                for (const auto& [token, asset] : package->attachmentMap)
                    if (token.empty() || token.size() > 64 || asset.empty() || asset.size() > 120 ||
                        token.find('\0') != std::string::npos || asset.find('\0') != std::string::npos)
                        throw std::runtime_error("invalid weapon attachment mapping");
                for (const auto& row : metadata.value("attachment_rows", nlohmann::json::array())) {
                    Package::AttachmentRow attachment{row.at("asset"), row.at("reference"),
                                                       row.at("token"), row.at("display_name")};
                    if (package->attachmentRows.size() >= 162 || attachment.asset.size() > 120 ||
                        attachment.reference.size() > 120 || attachment.token.size() > 64 || attachment.title.size() > 128)
                        throw std::runtime_error("invalid custom attachment row");
                    package->attachmentRows.push_back(std::move(attachment));
                }
            }
            for (const auto& group : {package->base, package->common})
                for (const char* prefix : {"", "techsets_", "ww_", "eng_"}) {
                    const auto zone = prefix + group;
                    const auto path = root / (zone + ".ff");
                    std::ifstream file(path, std::ios::binary);
                    std::array<unsigned char, 20> header{};
                    if (!file.read(reinterpret_cast<char*>(header.data()), header.size()) ||
                        memcmp(header.data(), "IWffc100\x0B\0\0\0\xF7\x0F\0\0\0\0\0\0",
                               header.size()))
                        throw std::runtime_error("missing or invalid custom weapon zone: " + zone);
                    const auto diskPath = path.u8string();
                    package->zonePaths.emplace(
                        zone, std::string(reinterpret_cast<const char*>(diskPath.data()),
                                          diskPath.size()));
                }
            package->titleKey = "CUSTOM_WEAPON/" + package->base;
            package->descriptionKey = "CUSTOM_WEAPON_DESC/" + package->base;
            packages.push_back(std::move(package));
        }
    } catch (const std::exception& error) {
        logger::Trace("Weapons", "registration disabled: %s", error.what());
        return hook::Status::Installed;
    }
    if (packages.empty())
        return hook::Status::Installed;
    std::sort(packages.begin(), packages.end(), [](const auto& a, const auto& b) {
        return a->slot < b->slot;
    });
    const auto status =
        hook::Install(reinterpret_cast<void*>(base + game::kDbLoadFastfilesRVA), &LoadWithWeapons,
                      game::kDbLoadFastfilesPrologue, game::kDbLoadFastfilesStolen, g_load);
    if (status == hook::Status::Installed) {
        g_packages = std::move(packages);
        g_available.store(true, std::memory_order_release);
        logger::Trace("Weapons", "registered %zu separate base-weapon packages", g_packages.size());
    }
    return status;
}

bool IsZone(const char* name) {
    if (!name || !g_available.load(std::memory_order_acquire))
        return false;
    return std::any_of(g_packages.begin(), g_packages.end(), [name](const auto& p) {
        return p->zonePaths.contains(name);
    });
}
bool ResolveDiskRead(const char* name, std::string& path) {
    if (!name || !g_available.load(std::memory_order_acquire))
        return false;
    const auto filename = std::filesystem::path(name).filename();
    if (filename.extension() != ".ff")
        return false;
    const auto zone = filename.stem().string();
    for (const auto& p : g_packages) {
        const auto found = p->zonePaths.find(zone);
        if (found != p->zonePaths.end()) {
            path = found->second;
            return true;
        }
    }
    return false;
}
void* ExtendAsset(int type, const char* name, void* source, uintptr_t callerRva) {
    if (!source || !name || (type != 43 && type != 54 && type != 57) ||
        !g_available.load(std::memory_order_acquire))
        return source;
    try {
        std::lock_guard lock(g_mutex);
        if (type == 43) {
            // Bounded read-only diagnostics in the existing DB lookup hook.
            // Distinguish selecting the stock gun from a custom asset whose
            // mesh pointers or stats were wrong after native zone loading.
            if (strncmp(name, "iw8_cw_", 7) && strcmp(name, "iw8_pi_mike1911_mp"))
                return source;
            static std::set<std::pair<std::string, uintptr_t>> seen;
            if (seen.size() >= 64 || !seen.emplace(name, callerRva).second)
                return source;
            std::array<uint8_t, 624> weapon{};
            const char* internal = nullptr;
            const uint8_t* definition = nullptr;
            if (!ReadArray(source, weapon.data(), weapon.size()))
                return source;
            memcpy(&internal, weapon.data(), 8);
            memcpy(&definition, weapon.data() + 8, 8);
            const auto modelName = [&](size_t offset) {
                const void* model = nullptr;
                const char* modelName = nullptr;
                if (!definition || !safemem::ReadBytes(definition + offset, &model, 8) || !model ||
                    !safemem::ReadBytes(model, &modelName, 8))
                    return std::string("<none>");
                return String(modelName);
            };
            int clip = 0;
            memcpy(&clip, weapon.data() + 0x1C0, sizeof(clip));
            LOG_INFO("Weapons",
                     "lookup '%s' -> '%s' clip=%d view='%s' world='%s' caller_rva=0x%llX", name,
                     String(internal).c_str(), clip, modelName(8).c_str(), modelName(0x38).c_str(),
                     static_cast<unsigned long long>(callerRva));
            return source;
        }
        return type == 54 ? ExtendTable(name, source) : ExtendDDL(name, source);
    } catch (const std::exception& error) {
        logger::Trace("Weapons", "'%s' left unchanged: %s", name, error.what());
        return source;
    }
}
}
