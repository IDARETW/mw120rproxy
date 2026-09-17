#pragma once
#include <string>

namespace iw8
{
// Native Replay melee package; input carries a prepared reference and OBJ geometry.
bool buildWeapon(const std::string &input, const std::string &outputDirectory);
// Separate from map argument parsing: weapon flags describe native asset fields.
int weaponMain(int argc, char **argv);
} // namespace iw8
