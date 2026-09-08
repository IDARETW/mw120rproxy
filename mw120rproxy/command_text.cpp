#include "command_text.h"
#include "dvar_names.h"
#include <cctype>

namespace commandtext {
std::string Translate(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    size_t pos = 0;
    unsigned tokenIndex = 0;
    bool dvarCommand = false;
    while (pos < input.size()) {
        const char ch = input[pos];
        if (ch == ';' || ch == '\n' || ch == '\r') {
            tokenIndex = 0;
            dvarCommand = false;
            output += ch;
            ++pos;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            output += ch;
            ++pos;
            continue;
        }
        // Preserve comments through the next line boundary.
        if (ch == '/' && pos + 1 < input.size() && input[pos + 1] == '/') {
            const auto end = input.find_first_of("\r\n", pos);
            output +=
                input.substr(pos, end == std::string_view::npos ? input.size() - pos : end - pos);
            pos = end == std::string_view::npos ? input.size() : end;
            continue;
        }
        const size_t start = pos;
        if (ch == '"') {
            ++pos;
            while (pos < input.size()) {
                if (input[pos] == '\\' && pos + 1 < input.size()) {
                    pos += 2;
                    continue;
                }
                if (input[pos++] == '"')
                    break;
            }
            output += input.substr(start, pos - start);
        } else {
            while (pos < input.size() && !std::isspace(static_cast<unsigned char>(input[pos])) &&
                   input[pos] != ';')
                ++pos;
            const auto token = input.substr(start, pos - start);
            if (tokenIndex == 0)
                dvarCommand = token == "set" || token == "seta" || token == "setu" ||
                              token == "sets" || token == "toggle" || token == "reset";
            const char* replacement = nullptr;
            if (tokenIndex == 0 && (token == "noclip" || token == "mw_noclip"))
                replacement = "cmd noclip";
            if ((tokenIndex == 0 && !dvarCommand) || (tokenIndex == 1 && dvarCommand))
                for (const auto& entry : dvardb::kEntries)
                    if (token == entry.name) {
                        replacement = entry.token;
                        break;
                    }
            output += replacement ? std::string_view(replacement) : token;
        }
        ++tokenIndex;
    }
    return output;
}
}
