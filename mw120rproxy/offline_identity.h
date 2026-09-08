#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace offlineauth {
// Replay's native sign-in getter at RVA 0x1665990 uses a 0xE8 stride.
// v1 name offsets are 0x2C/0x6C, not the comments on zeroproxy's v2 layout.
struct UserRecord {
    int32_t signInState;
    char name[36];
    uint32_t reserved28;
    char platformName[64];
    char fullName[36];
    uint64_t xuid;
    char xuidText[21];
    uint8_t reservedAD[11];
    uint64_t platformId;
    char platformIdText[21];
    uint8_t reservedD5[19];
};
static_assert(sizeof(UserRecord) == 0xE8);
static_assert(offsetof(UserRecord, platformName) == 0x2C);
static_assert(offsetof(UserRecord, fullName) == 0x6C);
static_assert(offsetof(UserRecord, xuid) == 0x90);
static_assert(offsetof(UserRecord, platformId) == 0xB8);

struct Identity {
    uint64_t xuid{};
    char name[36]{};
};

inline void PopulateUser(UserRecord& user, const Identity& identity) {
    strncpy_s(user.name, identity.name, _TRUNCATE);
    strncpy_s(user.platformName, identity.name, _TRUNCATE);
    strncpy_s(user.fullName, identity.name, _TRUNCATE);
    user.xuid = user.platformId = identity.xuid;
    sprintf_s(user.xuidText, "%llu", static_cast<unsigned long long>(identity.xuid));
    strcpy_s(user.platformIdText, user.xuidText);
    // Publish sign-in only after all identities are present.
    user.signInState = 2;
}

// Native OwnsBaseGame decode: RVA 0x1AC2609. The same XOR transform encodes.
// The address is that of the encoded DWORD, not that of the auth object.
inline uint32_t TransformOwnership(uint32_t value, uintptr_t address, uint32_t key) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t x = static_cast<uint32_t>(address + i) ^ key;
        const uint32_t product = x * (x + 2u);
        const uint32_t byte = ((value >> (8 * i)) ^ product ^ (product >> 8)) & 0xFFu;
        result |= byte << (8 * i);
    }
    return result;
}
}
