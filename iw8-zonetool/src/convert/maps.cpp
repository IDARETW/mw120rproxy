

#include "maps_convert.h"
#include "../common/log.h"
#include "../zonesrc/zone_source.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace convert {

bool iw3KeyToIw8KeyId(const std::string& iw3KeyLower, uint32_t& keyIdOut) {
    if (iw3KeyLower == "classname") {
        keyIdOut = 212;
        return true;
    }
    if (iw3KeyLower == "origin") {
        keyIdOut = 709;
        return true;
    }
    if (iw3KeyLower == "angles") {
        keyIdOut = 80;
        return true;
    }
    return false;
}

namespace {
// Minimal IW3 entityString tokenizer: a flat sequence of { ... } blocks where each block is a list
// of "key" "value" string pairs. Returns the parsed entities (vector of (key,value) pairs, in
// order). Mirrors the zonetool-develop IW3 MapEnts Entities::parse state machine.
struct KV {
    std::string key, val;
};
using Entity = std::vector<KV>;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

std::vector<Entity> parseIw3Entities(const std::string& buf) {
    enum { AWAIT_KEY, READ_KEY, AWAIT_VALUE, READ_VALUE };
    int state = AWAIT_KEY;
    std::string key, val;
    Entity cur;
    std::vector<Entity> out;
    for (char c : buf) {
        switch (c) {
        case '{':
            cur.clear();
            state = AWAIT_KEY;
            break;
        case '}':
            out.push_back(cur);
            cur.clear();
            state = AWAIT_KEY;
            break;
        case '"':
            if (state == AWAIT_KEY) {
                key.clear();
                state = READ_KEY;
            } else if (state == READ_KEY) {
                state = AWAIT_VALUE;
            } else if (state == AWAIT_VALUE) {
                val.clear();
                state = READ_VALUE;
            } else /* READ_VALUE */ {
                cur.push_back({key, val});
                state = AWAIT_KEY;
            }
            break;
        default:
            if (state == READ_KEY)
                key.push_back(c);
            else if (state == READ_VALUE)
                val.push_back(c);
            break;
        }
    }
    return out;
}
}

size_t iw3ToIw8EntityString(const std::string& in, std::string& out) {
    out.clear();
    std::vector<Entity> ents = parseIw3Entities(in);
    size_t emitted = 0;
    for (const Entity& e : ents) {
        std::string body;
        bool any = false;
        for (const KV& kv : e) {
            uint32_t keyId = 0;
            if (!iw3KeyToIw8KeyId(toLower(kv.key), keyId))
                continue; // drop unknown keys
            if (any)
                body.push_back(' ');
            body += std::to_string(keyId);
            body += " \"";
            body += kv.val;
            body += "\"";
            any = true;
        }
        if (!any)
            continue; // entity lost all keys -> drop
        out += "{ ";
        out += body;
        out += " }\n";
        ++emitted;
    }
    return emitted;
}

// ---- dynentitylist blob -------------------------------------------------------------------------
std::vector<uint8_t> serializeDynents(const std::vector<DynentRecord>& recs) {
    std::vector<uint8_t> payload;
    uint32_t count = static_cast<uint32_t>(recs.size());
    payload.resize(4 + recs.size() * sizeof(DynentRecord));
    std::memcpy(payload.data(), &count, 4);
    if (!recs.empty())
        std::memcpy(payload.data() + 4, recs.data(), recs.size() * sizeof(DynentRecord));
    return iw3sr::ZoneSource::wrapBlob("DYNENT", 1, payload.data(), payload.size());
}

bool parseDynents(const std::vector<uint8_t>& blob, std::vector<DynentRecord>& out) {
    out.clear();
    std::string magic;
    uint32_t version = 0;
    std::vector<uint8_t> payload;
    if (!iw3sr::ZoneSource::unwrapBlob(blob, magic, version, payload))
        return false;
    if (magic != "DYNENT") {
        zt::warn("dynent: bad magic '%s'", magic.c_str());
        return false;
    }
    if (payload.size() < 4)
        return false;
    uint32_t count = 0;
    std::memcpy(&count, payload.data(), 4);
    size_t need = 4 + static_cast<size_t>(count) * sizeof(DynentRecord);
    if (payload.size() < need) {
        zt::warn("dynent: truncated (%zu < %zu)", payload.size(), need);
        return false;
    }
    out.resize(count);
    if (count)
        std::memcpy(out.data(), payload.data() + 4, count * sizeof(DynentRecord));
    return true;
}

} // namespace convert
