#include "maps_convert.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace convert
{
bool iw3KeyToIw8KeyId(const std::string &key, uint32_t &keyId)
{
    if (key == "classname")
    {
        keyId = 212;
        return true;
    }
    if (key == "origin")
    {
        keyId = 709;
        return true;
    }
    if (key == "angles")
    {
        keyId = 80;
        return true;
    }
    return false;
}

namespace
{
struct KeyValue
{
    std::string key;
    std::string value;
};
using Entity = std::vector<KeyValue>;

constexpr const char *ReplayBaselineAnchor =
    "{ 212 \"script_model\" 709 \"0 0 16\" 80 \"0 0 0\" }\n";

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<Entity> parseEntities(const std::string &text)
{
    enum class State
    {
        awaitKey,
        readKey,
        awaitValue,
        readValue,
    };

    State state = State::awaitKey;
    std::string key;
    std::string value;
    Entity current;
    std::vector<Entity> entities;

    for (const char character : text)
    {
        switch (character)
        {
        case '{':
            current.clear();
            state = State::awaitKey;
            break;
        case '}':
            entities.push_back(current);
            current.clear();
            state = State::awaitKey;
            break;
        case '"':
            if (state == State::awaitKey)
            {
                key.clear();
                state = State::readKey;
            }
            else if (state == State::readKey)
            {
                state = State::awaitValue;
            }
            else if (state == State::awaitValue)
            {
                value.clear();
                state = State::readValue;
            }
            else
            {
                current.push_back({key, value});
                state = State::awaitKey;
            }
            break;
        default:
            if (state == State::readKey)
            {
                key.push_back(character);
            }
            else if (state == State::readValue)
            {
                value.push_back(character);
            }
            break;
        }
    }
    return entities;
}

std::map<std::string, size_t> iw8ClassCounts(const std::string &text)
{
    const std::regex entity(R"(\{([^{}]*)\})");
    const std::regex field(R"field((\d+)\s+"([^"]*)")field");
    std::map<std::string, size_t> counts;
    for (auto item = std::sregex_iterator(text.begin(), text.end(), entity);
         item != std::sregex_iterator(); ++item)
    {
        const std::string body = (*item)[1].str();
        for (auto pair = std::sregex_iterator(body.begin(), body.end(), field);
             pair != std::sregex_iterator(); ++pair)
        {
            if ((*pair)[1].str() == "212")
            {
                ++counts[lowercase((*pair)[2].str())];
                break;
            }
        }
    }
    return counts;
}

bool isReplayEntity(const std::string &classname)
{
    return classname == "worldspawn" || classname == "info_player_start" ||
           classname == "mp_global_intermission" || classname == "mp_tdm_spawn" ||
           classname == "mp_tdm_spawn_allies_start" ||
           classname == "mp_tdm_spawn_axis_start";
}
} // namespace

void validateIw8EntityString(const std::string &input)
{
    const auto counts = iw8ClassCounts(input);
    const auto require = [&](const char *classname) {
        const auto item = counts.find(classname);
        if (item == counts.end() || item->second == 0)
        {
            throw std::runtime_error(std::string("Missing required Replay entity: ") + classname);
        }
    };
    require("worldspawn");
    require("mp_tdm_spawn");
    require("mp_tdm_spawn_allies_start");
    require("mp_tdm_spawn_axis_start");
    require("script_model");
}

size_t iw3ToIw8EntityString(const std::string &input, std::string &output)
{
    output.clear();
    size_t emitted = 0;

    for (const Entity &entity : parseEntities(input))
    {
        const auto classname = std::find_if(entity.begin(), entity.end(), [](const KeyValue &pair) {
            return lowercase(pair.key) == "classname";
        });
        if (classname == entity.end())
        {
            continue;
        }

        const std::string name = lowercase(classname->value);
        if (!isReplayEntity(name))
        {
            continue;
        }

        std::string body;
        for (const KeyValue &pair : entity)
        {
            uint32_t keyId = 0;
            if (!iw3KeyToIw8KeyId(lowercase(pair.key), keyId))
            {
                continue;
            }
            if (!body.empty())
            {
                body.push_back(' ');
            }
            body += std::to_string(keyId);
            body += " \"";
            body += pair.value;
            body += "\"";
        }

        if (body.empty())
        {
            continue;
        }
        output += "{ " + body + " }\n";
        ++emitted;
    }

    // Baked world props and spawn markers do not enter Replay's non-player
    // snapshot baseline. Keep one harmless runtime entity so the client can
    // complete the baseline before the server switches to delta snapshots.
    output += ReplayBaselineAnchor;
    ++emitted;
    validateIw8EntityString(output);
    return emitted;
}
} // namespace convert
