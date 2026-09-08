#pragma once
#include <cstdint>
#include <cstddef>

namespace game {

constexpr uintptr_t kDbFileOpenRVA = 0xD8CA30;
constexpr uint8_t kDbFileOpenPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
};

constexpr uintptr_t kGdiCloneBootstrapRVA = 0x3061A0;
constexpr uint8_t kGdiCloneBootstrapPrologue[] = {
    0x40, 0x57, 0xB8, 0xC0, 0x33, 0x00, 0x00, 0xE8, 0x44, 0x6D, 0xEC, 0x01, 0x48, 0x2B, 0xE0,
};

constexpr uintptr_t kDvarRegisterBoolRVA = 0x13E7670;

constexpr uint8_t kDvarRegisterBoolPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x60,
};
constexpr size_t kDvarRegisterBoolStolen = sizeof(kDvarRegisterBoolPrologue);

using Dvar_RegisterBool_t = void*(__fastcall*)(const char* name,
                                               bool value,
                                               unsigned int flags,
                                               const char* description);

constexpr uintptr_t kDvarRegisterVariantRVA = 0x13E7D40;
constexpr uint8_t kDvarRegisterVariantPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
};
constexpr size_t kDvarRegisterVariantStolen = sizeof(kDvarRegisterVariantPrologue);

constexpr uintptr_t kDvarGenerateChecksumRVA = 0x13E66C0;

using Dvar_RegisterVariant_t = void*(__fastcall*)(const char* name,
                                                  unsigned int checksum,
                                                  int type,
                                                  unsigned int flags,
                                                  void* pValue,
                                                  void* pDomain,
                                                  const char* description);

inline const char* DvarTypeName(int t) {
    switch (t) {
    case 0:
        return "bool";
    case 1:
        return "float";
    default:
        return nullptr;
    }
}

constexpr uintptr_t kDbLoadFastfilesRVA = 0x11ACD30;
constexpr uint8_t kDbLoadFastfilesPrologue[] = {
    0x40, 0x55, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0x5C, 0x24, 0x50,
    0x48, 0x89, 0x74, 0x24, 0x58, 0x48, 0x89, 0x7C, 0x24, 0x60,
};
constexpr size_t kDbLoadFastfilesStolen = sizeof(kDbLoadFastfilesPrologue);

// The target database names this as luaL_loadfile_FastFile. The first 23 bytes
// end at an instruction boundary. Use replacement chunks only. Do not call Lua from C.
constexpr uintptr_t kLuaLoadfileFastfileRVA = 0x19BAEE0;
constexpr uint8_t kLuaLoadfileFastfilePrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA,
};
constexpr size_t kLuaLoadfileFastfileStolen = sizeof(kLuaLoadfileFastfilePrologue);

constexpr uintptr_t kLuiRegisterFunctionRVA = 0x19CDC60;
constexpr uint8_t kLuiRegisterFunctionPrologue[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x15, 0x42, 0xAC, 0xE9, 0x0F, 0x8B, 0xD9,
};
constexpr size_t kLuiRegisterFunctionStolen = sizeof(kLuiRegisterFunctionPrologue);

// The target string is "MatchRules.SetMatchRulesData called while not system link host".
constexpr uintptr_t kSystemLinkSetMatchRulesRVA = 0x19F25D0;
constexpr uint8_t kSystemLinkSetMatchRulesPrologue[] = {
    0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x48, 0x83, 0xEC,
    0x60, 0x48, 0x8B, 0x05, 0x17, 0x42, 0x99, 0x0B,
};
constexpr size_t kSystemLinkSetMatchRulesStolen = sizeof(kSystemLinkSetMatchRulesPrologue);

// The target database gives this per-frame match-rules callback.
constexpr uintptr_t kMatchRulesFrameRVA = 0x1040E50;
constexpr uint8_t kMatchRulesFramePrologue[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x15, 0x23, 0x56, 0x75, 0x0B, 0x48, 0x8B, 0xD9,
};
constexpr size_t kMatchRulesFrameStolen = sizeof(kMatchRulesFramePrologue);

// FS_FOpenFileReadCurrentThread(filename, fileHandle). The cleaned game-test source
// confirms that this wrapper sends the requested qpath to FS_FOpenFileReadForThread.
// The target database confirms this RVA and the 16-byte boundary below. This proxy uses
// it only as an opt-in request monitor. It does not redirect a zone path by default.
constexpr uintptr_t kFsFOpenFileReadCurrentThreadRVA = 0x13D9240;
constexpr uint8_t kFsFOpenFileReadCurrentThreadPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
};
constexpr size_t kFsFOpenFileReadCurrentThreadStolen =
    sizeof(kFsFOpenFileReadCurrentThreadPrologue);

// DB_LoadZone accepts a zone name. It is not a substitute for the System Link map path.
constexpr uintptr_t kDbLoadZoneRVA = 0x12279C0;

// This target function reports bad fastfile data header sentinels.
constexpr uintptr_t kDbAuthLoadInflateInternalRVA = 0xD89E10;
constexpr uint8_t kDbAuthLoadInflateInternalPrologue[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x8B, 0x81, 0x18, 0xE9,
    0x08, 0x00, 0x48, 0x8B, 0xD9, 0x45, 0x85, 0xC0, 0x75, 0x57,
};
constexpr size_t kDbAuthLoadInflateInternalStolen = sizeof(kDbAuthLoadInflateInternalPrologue);

// These FS helpers are map-exists candidates. Do not use them as a global bypass.
constexpr uintptr_t kFsFileExistsRVA = 0x16B2630;
constexpr uintptr_t kFsFileExistsWithPathRVA = 0x16B26A0;

// This GSC helper is not the menu map validator.
constexpr uintptr_t kGscrMapExistsRVA = 0x15E16B0;

constexpr uintptr_t kOmnvarRegisterFlagsFromStringTableRVA = 0xCD67C0;
}
