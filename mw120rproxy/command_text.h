#pragma once
#include <string>
#include <string_view>

namespace commandtext
{
    // Translate only command/dvar name tokens, preserving values and quoting.
    std::string Translate(std::string_view input);
}
