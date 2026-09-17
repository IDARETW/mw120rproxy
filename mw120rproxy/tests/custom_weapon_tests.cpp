// Exercises append-only registration against local, exact Replay table/DDL fixtures.
// No game is launched; no hooks are installed by this test binary.
#include <windows.h>
#include <chrono>
static size_t memoryQueries = 0;
static SIZE_T CountedVirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length) {
    ++memoryQueries;
    return VirtualQuery(address, info, length);
}
#define VirtualQuery CountedVirtualQuery
#include "../custom_weapons.cpp"
#undef VirtualQuery
#include <iostream>
#include <functional>
namespace logger {
void Trace(const char*, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}
}
namespace log120r {
void Linef(const char*, ...) {}
}
namespace hook::detail {
std::mutex& Mutex() {
    static std::mutex value;
    return value;
}
Status Prepare(void*, void*, const uint8_t*, size_t, void**) {
    return Status::Failed;
}
bool Enable(void*) {
    return false;
}
}
using Json = nlohmann::json;
void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
Json Read(const std::filesystem::path& path) {
    std::ifstream file(path);
    return Json::parse(file);
}
const char* Text(const Json& value) {
    return value.at("string").get_ref<const std::string&>().c_str();
}
DDLFile* nativeRecipe = nullptr;
DDLFile* NativeRecipeAsset(const char* name) {
    return !strcmp(name, "ddl/mp/recipes.ddl") ? nativeRecipe : nullptr;
}
void CheckNativeRecipe(const char* executable, DDLFile* stock, DDLFile* updated) {
    // Map the exact executable as a library without resolving imports or running
    // its entrypoint. Only its inspected, self-contained DDL library is called.
    const auto image = LoadLibraryExA(executable, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Check(image != nullptr, "cannot map Replay for native DDL checks");
    const auto base = reinterpret_cast<uintptr_t>(image);
    Check(!memcmp(reinterpret_cast<void*>(base + 0x2051750), "\xE9\x5B\x26\0\0", 5),
          "native DDL fixture does not match Replay");
    using ContextFn = bool (*)(void*, int, DDLDef*, void*, void*, void*);
    const auto reset = reinterpret_cast<ContextFn>(base + 0x20542C0);
    const auto create = reinterpret_cast<ContextFn>(base + 0x2051750);
    const auto init = reinterpret_cast<bool (*)(void*)>(base + 0x20520C0);
    const auto root = reinterpret_cast<void* (*)(void*, DDLDef*)>(base + 0x2051C70);
    const auto move = reinterpret_cast<bool (*)(void*, void*, uint32_t, void*)>(base + 0x2052410);
    const auto getBool = reinterpret_cast<bool (*)(void*, void*)>(base + 0x20517F0);
    std::array<uint8_t, 8192> scratch{};
    std::array<uint8_t, 136> config{};
    auto* scratchPtr = scratch.data();
    const int scratchSize = int(scratch.size());
    auto asset = &NativeRecipeAsset;
    memcpy(config.data(), &scratchPtr, 8);
    memcpy(config.data() + 8, &scratchSize, 4);
    memcpy(config.data() + 0x80, &asset, 8);
    nativeRecipe = updated;
    Check(init(config.data()), "native DDL init failed");
    std::array<uint8_t, 4096> buffer{};
    std::array<uint8_t, 48> context{};
    Check(reset(buffer.data(), int(buffer.size()), stock->definition, context.data(), nullptr,
                nullptr),
          "native recipe reset failed");
    struct Field {
        int offset, size, type, limit;
    };
    using Fields = std::map<std::string, Field>;
    const auto fields = [](DDLDef* def) {
        Fields result;
        std::function<void(int, int, const std::string&)> walk;
        walk = [&](int structure, int offset, const std::string& path) {
            const auto& s = def->structs[structure];
            for (int i = 0; i < s.count; ++i) {
                const auto& m = s.members[i];
                if (m.type == 11)
                    continue;
                Check(m.arraySize > 0 && m.bitSize % m.arraySize == 0, "invalid field extent");
                const int size = m.bitSize / m.arraySize;
                for (int j = 0; j < m.arraySize; ++j) {
                    const auto key =
                        path + "/" + m.name + (m.isArray ? "[" + std::to_string(j) + "]" : "");
                    const int bit = offset + m.offset + j * size;
                    if (m.type == 9)
                        walk(m.externalIndex, bit, key);
                    else
                        result.emplace(key,
                                       Field{bit + def->headerBitSize, size, m.type, m.limitSize});
                }
            }
        };
        walk(0, 0, "");
        return result;
    };
    const auto oldFields = fields(stock->definition), newFields = fields(updated->definition);
    // Seed nonzero values throughout the recipe, including both sides of every
    // relocated boundary. Enums use ordinal zero and floats stay zero.
    unsigned ordinal = 0;
    for (const auto& [path, field] : oldFields) {
        ++ordinal;
        if (field.type == 10 || field.type == 6 || field.type == 7)
            continue;
        if ((ordinal & 1) && field.size > 0)
            buffer[field.offset / 8] |= uint8_t(1u << (field.offset % 8));
    }
    const auto before = buffer;
    Check(create(buffer.data(), int(buffer.size()), updated->definition, context.data(), nullptr,
                 nullptr),
          "native conversion of stock recipe failed");
    for (const auto& [path, field] : oldFields) {
        const auto found = newFields.find(path);
        Check(found != newFields.end() && found->second.size == field.size,
              "stock recipe field lost");
        for (int bit = 0; bit < field.size; ++bit) {
            const int oldBit = field.offset + bit, newBit = found->second.offset + bit;
            Check(((before[oldBit / 8] >> (oldBit % 8)) & 1) ==
                      ((buffer[newBit / 8] >> (newBit % 8)) & 1),
                  ("native recipe conversion changed " + path).c_str());
        }
    }
    for (const auto& p : g_packages) {
        std::array<uint8_t, 32> state{};
        root(state.data(), updated->definition);
        for (const auto& part :
             {std::string("commonOption"), std::string("weaponRestricted"), p->base})
            Check(move(state.data(), state.data(), Hash(part, false), nullptr),
                  ("native recipe lookup failed: " + part).c_str());
        Check(!getBool(state.data(), context.data()), "new pistol inherited a stock restriction");
        int offset{};
        memcpy(&offset, state.data() + 4, 4);
        offset += updated->definition->headerBitSize;
        buffer[offset / 8] |= uint8_t(1u << (offset % 8));
        Check(getBool(state.data(), context.data()), "native custom restriction bit not readable");
        for (const auto& [path, field] : oldFields) {
            const auto& now = newFields.at(path);
            Check(offset < now.offset || offset >= now.offset + now.size,
                  "custom restriction overlaps stock recipe data");
        }
    }
    Check(create(buffer.data(), int(buffer.size()), updated->definition, context.data(), nullptr,
                 nullptr),
          "native reopening converted recipe failed");
    std::cout << "Native Replay recipe conversion and lookup passed: " << oldFields.size()
              << " stock fields preserved; independent custom restrictions\n";
    nativeRecipe = nullptr;
    FreeLibrary(image);
}
void CheckNativeWeaponNames(const char* executable, const std::filesystem::path& directory) {
    const auto stock = Read(directory / "weapon-names-current.json");
    std::vector<std::string> strings = stock.at("strings").get<std::vector<std::string>>();
    std::vector<const char*> names;
    for (const auto& name : strings)
        names.push_back(name.c_str());
    struct Names {
        const char* name;
        uint32_t type, source, flags, count;
        const char** strings;
    };
    static_assert(sizeof(Names) == 32);
    const char* customName = "iw8_cw_pocket_blaster_mp";
    Names original{"ncs_wep_global_stream_mp", 14, 0, 0, uint32_t(names.size()), names.data()};
    Names custom{"ncs_wep_zz_iw8_cw_pocket_blaster", 14, 0, 0, 1, &customName};
    struct Node {
        Names* names;
        Node* next;
    } second{&custom, nullptr}, first{&original, nullptr};
    const auto image = LoadLibraryExA(executable, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Check(image != nullptr, "cannot map Replay for native weapon-name check");
    const auto base = reinterpret_cast<uintptr_t>(image);
    Check(!memcmp(reinterpret_cast<void*>(base + 0x10F0750), "\xBA\x0E\0\0\0\x44\x8D\x42\xF3", 9),
          "native weapon-name enumerator does not match Replay");
    const auto compare = reinterpret_cast<int (*)(Names**, Names**)>(base + 0x10EFE40);
    auto* a = &original;
    auto* b = &custom;
    Check(compare(&a, &b) < 0, "custom common list would precede stock weapon names");
    auto** head = reinterpret_cast<Node**>(base + 0xC6E0950 + 14 * 24);
    *head = &first;
    static std::vector<std::pair<unsigned, std::string>> visited;
    const auto collect = +[](unsigned index, const char* name) {
        visited.emplace_back(index, name);
    };
    const auto enumerate = reinterpret_cast<void (*)(decltype(collect))>(base + 0x10F0750);
    visited.clear();
    enumerate(collect);
    Check(visited.size() == names.size(), "stock weapon-name enumeration failed");
    first.next = &second;
    visited.clear();
    enumerate(collect);
    Check(visited.size() == names.size() + 1, "custom weapon not visited by native registration");
    for (size_t i = 0; i < names.size(); ++i)
        Check(visited[i].first == i + 1 && visited[i].second == names[i],
              "stock weapon index changed");
    Check(visited.back().first == names.size() + 1 && visited.back().second == customName,
          "native custom weapon index/name mismatch");
    *head = nullptr;
    FreeLibrary(image);
    std::cout << "Native Replay weapon-name enumeration passed: " << names.size()
              << " stock names preserved; custom weapon assigned the next index\n";
}
void CheckZoneLifetimes() {
    static std::vector<ZoneInfo> captured;
    g_load =
        +[](const ZoneInfo* zones, uint32_t count, uint32_t mode, bool parameter) -> uintptr_t {
        Check(mode == 7 && parameter, "zone load parameters changed");
        captured.assign(zones, zones + count);
        return 123;
    };
    for (const bool common : {false, true}) {
        const ZoneInfo owner{common ? "common_mp" : "global_stream_mp", common ? 4u : 2u, 1, 65535,
                             0};
        Check(LoadWithWeapons(&owner, 1, 7, true) == 123, "zone load result changed");
        Check(captured.size() == 1 + g_packages.size(), "wrong lifetime group size");
        for (size_t i = 0; i < g_packages.size(); ++i) {
            const auto& expected = common ? g_packages[i]->common : g_packages[i]->base;
            const auto& actual = captured[i + 1];
            Check(expected == actual.name && actual.flags == owner.flags &&
                      actual.failureMode == owner.failureMode && actual.priority == owner.priority,
                  "weapon/SFX zone does not inherit the corresponding stock lifetime");
        }
        // Native callers can already include custom zones; never queue them twice.
        const auto withCustom = captured;
        LoadWithWeapons(withCustom.data(), uint32_t(withCustom.size()), 7, true);
        Check(captured.size() == withCustom.size(), "custom zone request duplicated");
    }
    g_load = nullptr;
    std::cout
        << "Weapon and SFX zones preserve their separate native owner flags and load policy\n";
}
int main(int argc, char** argv) try {
    Check(argc == 2 || argc == 3,
          "pass the local fixture directory and optional Replay executable");
    const std::filesystem::path directory = argv[1];
    for (const auto slot : {61u, 70u}) {
        auto p = std::make_unique<Package>();
        p->base = "iw8_cw_test_" + std::to_string(slot);
        p->common = p->base + "_common";
        p->asset = p->base + "_mp";
        p->reference = "iw8_pi_mike1911_mp";
        p->titleKey = "CUSTOM_WEAPON/" + p->base;
        p->descriptionKey = "CUSTOM_WEAPON_DESC/" + p->base;
        p->slot = slot;
        for (const auto& group : {p->base, p->common})
            for (const char* prefix : {"", "techsets_", "ww_", "eng_"}) {
                const auto zone = prefix + group;
                p->zonePaths.emplace(zone, "C:/test/zone/" + zone + ".ff");
            }
        g_packages.push_back(std::move(p));
    }
    g_available = true;
    CheckZoneLifetimes();
    for (const auto& p : g_packages)
        for (const auto& [zone, disk] : p->zonePaths) {
            std::string resolved;
            Check(customweapons::IsZone(zone.c_str()), "native companion decoder not registered");
            Check(customweapons::ResolveDiskRead((zone + ".ff").c_str(), resolved) &&
                      resolved == disk,
                  "native companion disk path not registered");
        }
    std::string unchanged = "unchanged";
    for (const char* name : {"global_stream_mp", "eng_global_stream_mp", "iw8_cw_unknown",
                             "eng_iw8_cw_test_61_extra"}) {
        Check(!customweapons::IsZone(name), "stock or unknown zone intercepted");
        Check(!customweapons::ResolveDiskRead((std::string(name) + ".ff").c_str(), unchanged) &&
                  unchanged == "unchanged",
              "stock or unknown disk path intercepted");
    }
    const auto tables = Read(directory / "table-fixtures.json");
    for (const auto& [name, rows] : tables.items()) {
        g_tables.clear();
        TableCopy fixture;
        for (const auto& row : rows)
            for (const auto& value : row)
                fixture.cells.push_back(fixture.Intern(value.get<std::string>()));
        fixture.table = {name.c_str(),
                         int(rows[0].size()),
                         int(rows.size()),
                         int(fixture.pointers.size()),
                         0,
                         fixture.cells.data(),
                         fixture.hashes.data(),
                         fixture.pointers.data()};
        const auto original = fixture.table;
        auto* result =
            static_cast<Table*>(customweapons::ExtendAsset(54, name.c_str(), &fixture.table));
        Check(result != &fixture.table, "table registration was rejected");
        Check(result->rows == original.rows + 2 && result->columns == original.columns,
              "bad added row count");
        for (size_t cell = 0; cell < fixture.cells.size(); ++cell)
            Check(!strcmp(original.strings[original.cells[cell]],
                          result->strings[result->cells[cell]]),
                  "stock table value changed");
        Check(!memcmp(&original, &fixture.table, sizeof(Table)), "stock table header mutated");
        for (size_t i = 0; i < g_packages.size(); ++i) {
            const size_t begin = (original.rows + i) * original.columns;
            const auto& p = g_packages[i];
            const bool stats = original.columns == 61;
            Check(p->base == result->strings[result->cells[begin + (stats ? 4 : 1)]],
                  "custom row points at stock gun");
            if (stats) {
                Check(!*result->strings[result->cells[begin + 9]], "default attachments retained");
                Check(!strcmp(result->strings[result->cells[begin + 52]], "1"),
                      "attachment editor still enabled");
            }
        }
        Check(customweapons::ExtendAsset(54, name.c_str(), &fixture.table) == result,
              "table lookup cache is unstable");
        if (original.columns == 61) {
            // Reproduce Replay GSC's ID lookup and stop condition. Merely
            // checking appended rows misses weapons invisible to this scan.
            std::set<std::string> registered;
            for (int id = 1;; ++id) {
                const auto key = std::to_string(id);
                int row = 0;
                for (; row < result->rows; ++row)
                    if (key == result->strings[result->cells[row * result->columns]])
                        break;
                if (row == result->rows)
                    break;
                registered.emplace(result->strings[result->cells[row * result->columns + 4]]);
            }
            for (const auto& package : g_packages)
                Check(registered.contains(package->base),
                      "native script ID scan cannot discover custom weapon");
        }
        std::cout << name << ": all " << original.rows << " stock rows preserved; two new rows\n";
    }
    const auto attachmentTables = Read(directory / "attachment-table-fixtures.json");
    for (auto& package : g_packages) {
        package->attachments = true;
        package->defaultAttachments = "rec";
        package->attachmentMap = {{"xmagslrg", package->base + "/mag"}, {"adsidle", package->base + "/focus"}};
        package->attachmentRows = {{package->base + "/mag", "xmags_mike1911", "xmagslrg", "Custom magazine"}};
    }
    for (const auto& [name, rows] : attachmentTables.items()) {
        g_tables.clear();
        TableCopy fixture;
        for (const auto& row : rows)
            for (const auto& value : row)
                fixture.cells.push_back(fixture.Intern(value.get<std::string>()));
        fixture.table = {name.c_str(), int(rows[0].size()), int(rows.size()), int(fixture.pointers.size()),
                         0, fixture.cells.data(), fixture.hashes.data(), fixture.pointers.data()};
        const auto original = fixture.table;
        auto* result = static_cast<Table*>(customweapons::ExtendAsset(54, name.c_str(), &fixture.table));
        Check(result != &fixture.table && result->rows == original.rows + 2, "attachment table rows missing");
        for (int row = 0; row < original.rows; ++row)
            for (int col = 0; col < original.columns; ++col)
                Check(!strcmp(original.strings[original.cells[row * original.columns + col]],
                              result->strings[result->cells[row * result->columns + col]]),
                      "attachment registration changed a stock value");
        const auto value = [&](int row, int col) { return result->strings[result->cells[row * result->columns + col]]; };
        for (size_t i = 0; i < g_packages.size(); ++i) {
            const auto& package = g_packages[i];
            const int row = original.rows + int(i);
            if (original.columns == 163) {
                Check(package->base == value(row, 0), "missing per-weapon attachment map");
                Check(result->columns == original.columns + 1 && !strcmp(value(0, original.columns), "adsidle"),
                      "shared perk column was not appended");
                Check(package->base + "/focus" == value(row, original.columns), "shared perk override missing");
                for (int stockRow = 1; stockRow < original.rows; ++stockRow)
                    Check(!*value(stockRow, original.columns), "custom perk override leaked into stock weapons");
                for (int column = 1; column < original.columns; ++column)
                    Check(!strcmp(value(0,column),"xmagslrg") ? package->base + "/mag" == value(row,column)
                                                            : !*value(row,column),
                          "attachment map leaked another loadout token");
            } else if (original.columns == 33) {
                Check(package->base + "/mag" == value(row,4) && !strcmp(value(row,5),"xmagslrg"),
                      "owned attachment has incorrect asset/token identity");
                Check("CUSTOM_ATTACHMENT/" + package->base + "/mag" == value(row,3),
                      "owned attachment is missing its localization key");
                Check(std::to_string(row) == value(row,0), "attachment IDs are not contiguous");
            } else {
                Check(!strcmp(value(row,9),"rec"), "default assembly was discarded");
                int source = 0;
                for (; source < original.rows && strcmp(value(source,4),"iw8_pi_mike1911"); ++source) {}
                Check(source < original.rows && !strcmp(value(row,52),value(source,52)),
                      "native Gunsmith enable flag was not retained");
            }
        }
        const auto queriesBefore = memoryQueries;
        const auto before = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < 20000; ++repeat)
            Check(customweapons::ExtendAsset(54,name.c_str(),&fixture.table) == result, "attachment cache is unstable");
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - before).count();
        Check(memoryQueries == queriesBefore, "cached table lookup queried the process memory map");
        std::cout << name << ": 20000 cache hits in " << elapsed << " us; zero memory-map queries\n";
        // A native pool slot can retain its address while its zone data moves.
        auto reloadedCells = fixture.cells;
        fixture.table.cells = reloadedCells.data();
        auto* reloaded = static_cast<Table*>(customweapons::ExtendAsset(54,name.c_str(),&fixture.table));
        Check(reloaded != result && reloaded != &fixture.table, "zone reload reused a stale table copy");
        Check(reloaded->rows == result->rows && reloaded->columns == result->columns,
              "zone reload changed registered dimensions");
        fixture.table = original;
        Check(!memcmp(&original,&fixture.table,sizeof(Table)), "stock attachment table header mutated");
        std::cout << name << ": native attachment mapping and owned entries passed\n";
    }
    for (auto& package : g_packages) {
        package->attachments = false;
        package->defaultAttachments.clear();
        package->attachmentMap.clear();
        package->attachmentRows.clear();
    }
    g_tables.clear();
    for (const char* filename : {"playerdata-current.json", "privateloadouts-current.json",
                                 "rankedloadouts-current.json", "recipes-current.json"}) {
        g_ddls.clear();
        const auto input = Read(directory / filename);
        const auto& def = input.at("ddlDef").at("values")[0];
        std::vector<DDLEnum> enums(def.at("enumCount").get<size_t>());
        std::vector<std::vector<const char*>> enumStrings(enums.size());
        std::deque<std::vector<DDLHash>> hashStorage;
        const auto readHashes = [&](const Json& table) {
            auto& values = hashStorage.emplace_back();
            if (table.at("count").get<int>())
                for (const auto& h : table.at("list").at("values"))
                    values.push_back({h.at("hash"), h.at("index")});
            return DDLHashTable{values.data(), int(values.size()), int(values.size())};
        };
        for (size_t i = 0; i < enums.size(); ++i) {
            const auto& e = def.at("enumList").at("values")[i];
            for (const auto& value : e.at("members").at("values"))
                enumStrings[i].push_back(Text(value));
            enums[i] = {Text(e.at("name")), int(enumStrings[i].size()), 0, enumStrings[i].data(),
                        readHashes(e.at("hashTable"))};
        }
        std::vector<DDLStruct> structs(def.at("structCount").get<size_t>());
        std::vector<std::vector<DDLMember>> members(structs.size());
        for (size_t i = 0; i < structs.size(); ++i) {
            const auto& s = def.at("structList").at("values")[i];
            for (const auto& m : s.at("members").at("values")) {
                DDLMember record{};
                record.name = Text(m.at("name"));
                record.index = m.at("index");
                record.bitSize = m.at("bitSize");
                record.limitSize = m.at("limitSize");
                record.rangeLimit = m.at("rangeLimit");
                record.offset = m.at("offset");
                record.type = m.at("type");
                record.externalIndex = m.at("externalIndex");
                record.isArray = m.at("isArray");
                record.arraySize = m.at("arraySize");
                record.enumIndex = m.at("enumIndex");
                members[i].push_back(record);
            }
            structs[i] = {Text(s.at("name")),
                          s.at("bitSize"),
                          int(members[i].size()),
                          members[i].data(),
                          readHashes(s.at("hashTableUpper")),
                          readHashes(s.at("hashTableLower"))};
        }
        DDLDef fixture{};
        fixture.name = Text(def.at("name"));
        fixture.version = def.at("version");
        fixture.guid = def.at("guid");
        fixture.bitSize = def.at("bitSize");
        fixture.byteSize = def.at("byteSize");
        fixture.structs = structs.data();
        fixture.structCount = int(structs.size());
        fixture.enums = enums.data();
        fixture.enumCount = int(enums.size());
        fixture.headerBitSize = def.at("headerBitSize");
        fixture.headerByteSize = def.at("headerByteSize");
        DDLFile source{Text(input.at("name")), &fixture};
        const auto original = fixture;
        auto* result = static_cast<DDLFile*>(customweapons::ExtendAsset(57, source.name, &source));
        Check(result != &source, "DDL registration was rejected");
        if (!strcmp(filename, "recipes-current.json")) {
            Check(result->definition->version == (0x8000 | 463) &&
                      result->definition->byteSize == 4024 && result->definition->next == &fixture,
                  "recipe version/size/migration chain is incorrect");
            Check(!memcmp(&original, &fixture, sizeof(fixture)), "stock recipe definition mutated");
            if (argc == 3)
                CheckNativeRecipe(argv[2], &source, result);
            continue;
        }
        auto expected = fixture;
        expected.enums = result->definition->enums;
        expected.enumCount += 1;
        expected.structs = result->definition->structs;
        Check(!memcmp(&expected, result->definition, sizeof(expected)),
              "DDL offsets, GUID, version or layout changed");
        Check(!memcmp(&original, &fixture, sizeof(fixture)), "stock DDL mutated");
        for (size_t i = 0; i < enums.size(); ++i) {
            Check(!memcmp(&enums[i], &result->definition->enums[i], sizeof(DDLEnum)),
                  "stock enum changed");
            if (strcmp(enums[i].name, "LoadoutWeapon"))
                continue;
            const auto& added = result->definition->enums[fixture.enumCount];
            for (size_t si = 0; si < structs.size(); ++si) {
                const auto& updatedStruct = result->definition->structs[si];
                auto expectedStruct = structs[si];
                expectedStruct.members = updatedStruct.members;
                Check(!memcmp(&expectedStruct, &updatedStruct, sizeof(DDLStruct)),
                      "saved struct layout changed");
                for (size_t mi = 0; mi < members[si].size(); ++mi) {
                    auto expectedMember = members[si][mi];
                    if (expectedMember.type == 10 && expectedMember.externalIndex == int(i))
                        expectedMember.externalIndex = fixture.enumCount;
                    Check(!memcmp(&expectedMember, &updatedStruct.members[mi], sizeof(DDLMember)),
                          "saved member layout or progression array changed");
                }
            }
            Check(added.count == 71, "explicit stable slots were not honored");
            for (int k = 0; k < 61; ++k)
                Check(!strcmp(added.members[k], enums[i].members[k]), "stock enum ordinal changed");
            for (const auto& p : g_packages)
                Check(p->base == added.members[p->slot], "wrong custom enum slot");
            const auto& stockHashes =
                def.at("enumList").at("values")[i].at("hashTable").at("list").at("values");
            for (const auto& h : stockHashes) {
                const auto index = h.at("index").get<size_t>();
                Check(Hash(added.members[index], false) == h.at("hash").get<uint32_t>(),
                      "native DDL hash disagrees with reference");
            }
        }
        Check(customweapons::ExtendAsset(57, source.name, &source) == result,
              "DDL lookup cache is unstable");
        std::cout << source.name
                  << ": stock ordinals, hashes and save layout preserved; custom slots 61 and 70\n";
    }
    if (argc > 2)
        CheckNativeWeaponNames(argv[2], directory);
    std::cout << "Weapon registration fixture checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
