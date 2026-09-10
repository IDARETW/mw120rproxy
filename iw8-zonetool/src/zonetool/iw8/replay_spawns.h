#pragma once
#include "iw8_zonebuffer.h"
#include <array>
#include <cmath>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace iw8
{
// Replay Load_SpawnPointRecordList E0DB80: ushort count, pointer at +8;
// records are 40 bytes, aligned to four, with three remapped scr_string_t fields.
struct ReplaySpawnRecord
{
    uint16_t index{}, padding{};
    uint32_t name{}, target{}, noteworthy{};
    std::array<float, 3> origin{}, angles{};
};
static_assert(sizeof(ReplaySpawnRecord) == 40, "Replay spawn ABI");

struct ReplaySpawns
{
    // Null and the empty string are distinct in Scr_GetString. Map spawn scripts
    // pass target/noteworthy to string helpers even when their text is empty.
    std::vector<std::string> strings{std::string{}, std::string{}};
    std::vector<ReplaySpawnRecord> records;
    uint32_t intern(const std::string &s)
    {
        for (size_t i = 1; i < strings.size(); ++i)
            if (strings[i] == s)
                return static_cast<uint32_t>(i);
        strings.push_back(s);
        return static_cast<uint32_t>(strings.size() - 1);
    }
    explicit ReplaySpawns(const std::string &ents)
    {
        const std::regex entity(R"(\{([^{}]*)\})");
        const std::regex field(R"field((\d+)\s+"([^"]*)")field");
        for (auto e = std::sregex_iterator(ents.begin(), ents.end(), entity);
             e != std::sregex_iterator(); ++e)
        {
            const auto body = (*e)[1].str();
            std::map<unsigned, std::string> fields;
            for (auto f = std::sregex_iterator(body.begin(), body.end(), field);
                 f != std::sregex_iterator(); ++f)
                fields[std::stoul((*f)[1].str())] = (*f)[2].str();
            const auto &classname = fields[212];
            if (classname.compare(0, 3, "mp_") || classname.find("_spawn") == std::string::npos)
                continue;
            if (records.size() >= 65535)
                throw std::runtime_error("Too many compiled spawn points");
            ReplaySpawnRecord r{};
            r.index = static_cast<uint16_t>(records.size());
            r.name = intern(classname);
            r.target = intern(fields[1070]);
            r.noteworthy = intern(fields[845]);
            auto vector = [&](unsigned key, std::array<float, 3> &v, bool required) {
                if (fields[key].empty() && !required)
                    return;
                std::istringstream in(fields[key]);
                for (auto &x : v)
                    if (!(in >> x) || !std::isfinite(x))
                        throw std::runtime_error("Invalid spawn vector");
                std::string tail;
                if (in >> tail)
                    throw std::runtime_error("Trailing spawn vector data");
            };
            vector(709, r.origin, true);
            vector(80, r.angles, false);
            records.push_back(r);
        }
    }
    void writeStrings(ZoneBuffer &zb) const
    {
        if (records.empty())
            return;
        // Load_ScriptStringList DD1E90: virtual aligned XString pointers, then strings.
        zb.align(7);
        for (size_t i = 0; i < strings.size(); ++i)
            zb.writeT<uint64_t>(i ? PTR_FOLLOWS : PTR_NULL);
        for (size_t i = 1; i < strings.size(); ++i)
            zb.writeStr(strings[i].c_str());
    }
    void writeRecords(ZoneBuffer &zb) const
    {
        if (records.empty())
            return;
        zb.align(3);
        zb.write(records.data(), records.size() * sizeof(ReplaySpawnRecord));
    }
};
} // namespace iw8
