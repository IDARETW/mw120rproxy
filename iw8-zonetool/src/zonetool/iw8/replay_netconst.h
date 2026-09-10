#pragma once
#include <cstddef>
// Exact Replay type/tag table at RVA 0x4598378 (41 records, 24-byte stride).
// The older reference branch uses different ordinals; do not copy its enum.
namespace replayncs
{
inline constexpr unsigned AssetType = 61;
inline constexpr size_t BodySize = 0x20;
inline constexpr unsigned LevelSource = 2;
inline constexpr const char *Tags[] = {
    "mdl", // 0
    "mat", // 1
    "img", // 2
    "rmb", // 3
    "rmg", // 4
    "veh", // 5
    "vfx", // 6
    "loc", // 7
    "shk", // 8
    "tag", // 9
    "sic", // 10
    "hic", // 11
    "obj", // 12
    "mic", // 13
    "wep", // 14
    "hnt", // 15
    "anm", // 16
    "acl", // 17
    "nms", // 18
    "lui", // 19
    "sut", // 20
    "cam", // 21
    "hol", // 22
    "ges", // 23
    "tgt", // 24
    "vsn", // 25
    "atr", // 26
    "ccr", // 27
    "xac", // 28
    "cin", // 29
    "acc", // 30
    "bs2", // 31
    "cmo", // 32
    "pas", // 33
    "ait", // 34
    "sbd", // 35
    "xcm", // 36
    "exx", // 37
    "cob", // 38
    "stk", // 39
    "vcm", // 40
};
inline constexpr unsigned Count = sizeof(Tags) / sizeof(Tags[0]);
static_assert(Count == 41);
} // namespace replayncs
