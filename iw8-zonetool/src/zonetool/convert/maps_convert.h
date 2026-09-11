#pragma once

#include <cstdint>
#include <string>

namespace convert
{
bool iw3KeyToIw8KeyId(const std::string &key, uint32_t &keyId);
size_t iw3ToIw8EntityString(const std::string &input, std::string &output);
void validateIw8EntityString(const std::string &input);
} // namespace convert
