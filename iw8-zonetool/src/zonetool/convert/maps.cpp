#include "maps_convert.h"

#include <algorithm>
#include <cctype>
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
} // namespace

size_t iw3ToIw8EntityString(const std::string &input, std::string &output)
{
    output.clear();
    size_t emitted = 0;

    for (const Entity &entity : parseEntities(input))
    {
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
    return emitted;
}
} // namespace convert