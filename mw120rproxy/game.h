#pragma once
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// Game-specific constants for MW2019 retail 1.20.4.7623265-replay
// (game_dx12_ship_replay.exe).
//
// UNRESOLVED. Unlike mw124proxy/mw164proxy/mw169proxy at their starting point, this build was
// never previously reverse-engineered by this project. It reportedly can be statically
// analyzed (possibly not Arxan-.text-encrypted the way 1.24/1.64/1.69 are) — that is an
// INFERRED hypothesis, not verified (see README "Open questions"). A full IDA database
// exists on disk:
//   matching Replay 1.20 analysis database
// Use it (headless idat, per the project's re-kaparthy method: verify vs infer) to resolve
// the RVA + capture the real decrypted/on-disk prologue bytes below, together — a prologue
// array of the wrong length or content makes InstallHook's safemem::ReadBytes+memcmp check
// fail closed (never patches), so half-filling this in is safe but useless.
//
// InstallHook() (dvar_patches.cpp) treats RVA==0 as "unknown -> skip" and logs a warning once;
// it never patches address 0. Every constant below is a placeholder until filled in.
// ---------------------------------------------------------------------------

namespace game
{
    // DB_File_OpenDBFile(DBFileHandle*, filename): DCache -> FileStream_OpenFile.
    // This is the fastfile path, distinct from FS_FOpenFileReadCurrentThread.
    // Verified raw Replay bytes and both return paths: evidence/fastfile_open_target.json.
    constexpr uintptr_t kDbFileOpenRVA = 0xD8CA30;
    constexpr uint8_t kDbFileOpenPrologue[] = {
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,
    };
    // Replay startup privately maps gdi32.dll then calls its GdiDllInitialize.
    // Owner crash: return RVA 0x3153FE (sub_1403061A0+0xF25E), null execute,
    // before splash or dvar registration. On current Windows this export uses a
    // loader-initialized dispatch thunk, so a private copy cannot be initialized
    // this way. IW8-1.20-main/Main.cpp skips the same constructor at attach.
    // Proof: evidence/startup_bootstrap.cpp and startup_call_chain.md.
    constexpr uintptr_t kGdiCloneBootstrapRVA = 0x3061A0;
    constexpr uint8_t kGdiCloneBootstrapPrologue[] = {
        0x40,0x57,0xB8,0xC0,0x33,0x00,0x00,0xE8,0x44,0x6D,0xEC,0x01,0x48,0x2B,0xE0,
    };
    // ==== Dvar_RegisterBool : RVA 0x13E7670 (1.20.4.7623265-replay) — VERIFIED ====
    // Resolved 2026-07-14 by static RE of game_dx12_ship_replay.exe.i64 (imagebase 0x140000000,
    // so RVA = ea - 0x140000000). Confirmed by independent disassembly (tools/ida_dvar120*.py):
    // sub_1413E7670 is a 4-arg wrapper (rcx=name, dl=value, r8d=flags, r9=desc) that stores the
    // bool into a stack DvarValue, calls Dvar_GenerateChecksum(name) (0x13E66C0), forces the dvar
    // TYPE to 0 (xor r8d,r8d = DVAR_TYPE_BOOL), and tail-calls the 7-arg core Dvar_RegisterVariant
    // (0x13E7D40) — exactly the Dvar_RegisterBool shape. (This build ships with readable dvar
    // strings + honest .text in the .i64 = statically analyzable, not Arxan-encrypted on disk;
    // the prologue-verify below still gates the patch, so it is safe either way.)
    //
    // Provenance note: the archived "Project Donetsk" hook_lib (IW8-1.20-main) also targets
    // Dvar_RegisterBool at raw VA 0x1413E7670 (RVA 0x13E7670). That external hint pointed at the
    // right address, but the value here was VERIFIED against our own .i64 (Hex-Rays mis-marks the
    // function tail as data → a bogus JUMPOUT decompile; the raw bytes decode cleanly), per ADR-6.
    constexpr uintptr_t kDvarRegisterBoolRVA = 0x13E7670;

    // Verified prologue (1.20.4-replay .i64, clean instruction boundary at +15):
    //   48 89 5C 24 08    mov  [rsp+8],  rbx
    //   48 89 74 24 10    mov  [rsp+10], rsi
    //   57                push rdi
    //   48 83 EC 60       sub  rsp, 60h        (next: 33 C0 xor eax,eax @ +15 -> clean boundary)
    // All 15 bytes are position-independent (no rip-relative / relative branch) -> safe to steal.
    constexpr uint8_t kDvarRegisterBoolPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x74, 0x24, 0x10,
        0x57,
        0x48, 0x83, 0xEC, 0x60,
    };
    constexpr size_t  kDvarRegisterBoolStolen = sizeof(kDvarRegisterBoolPrologue); // 15

    // Dvar_RegisterBool(name, value, flags, description) -> dvar* ; x64 __fastcall — VERIFIED for
    // this build (rcx=name, dl=value, r8d=flags, r9=desc via the disasm above).
    using Dvar_RegisterBool_t = void*(__fastcall*)(const char* name, bool value,
                                                    unsigned int flags, const char* description);

    // ==== Dvar_RegisterVariant : RVA 0x13E7D40 (1.20.4.7623265-replay) — VERIFIED ====
    // The 7-arg core funnel behind Dvar_RegisterBool/Int/Float/String/... (Dvar_RegisterBool
    // tail-calls it via the rel32 at 0x13E76DF). Hex-Rays confirms the 7-param signature:
    // (name, checksum, type, flags, pValue, pDomain, desc). Only needed for the optional
    // hook_variant trace (mw120rproxy.trace.log); not required for the bool override.
    // Verified prologue (clean instruction boundary at +14):
    //   48 89 5C 24 20    mov  [rsp+20h], rbx
    //   55 56 57          push rbp / rsi / rdi
    //   41 54 41 55 41 56 push r12 / r13 / r14   (next: 41 57 push r15 @ +14 -> clean boundary)
    constexpr uintptr_t kDvarRegisterVariantRVA = 0x13E7D40;
    constexpr uint8_t   kDvarRegisterVariantPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20,
        0x55, 0x56, 0x57,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    };
    constexpr size_t    kDvarRegisterVariantStolen = sizeof(kDvarRegisterVariantPrologue); // 14

    // Dvar_GenerateChecksum(name) -> checksum : RVA 0x13E66C0 (VERIFIED; called by Dvar_RegisterBool
    // at 0x13E76B6, result -> the checksum arg of Dvar_RegisterVariant). Naming matches mw164proxy's
    // Dvar_GenerateChecksum. Not hooked — recorded for the RVA map / future dvar-by-hash work.
    constexpr uintptr_t kDvarGenerateChecksumRVA = 0x13E66C0;

    using Dvar_RegisterVariant_t = void*(__fastcall*)(const char* name, unsigned int checksum,
                                                       int type, unsigned int flags, void* pValue,
                                                       void* pDomain, const char* description);

    // bool=0/float=1 confirmed on every other IW8 build; nullptr for the rest -> caller shows
    // the raw type number.
    inline const char* DvarTypeName(int t)
    {
        switch (t) { case 0: return "bool"; case 1: return "float"; default: return nullptr; }
    }

    // ==== Custom-map flow, target database pass (2026-08-24) ================================
    // Source: matching Replay 1.20 analysis database.
    // The values below are address records. No feature calls or hooks them yet.

    // DB_LoadFastfiles. The target string is "DB_LoadFastfiles() Job:".
    // The first 21 bytes end at an instruction boundary.
    constexpr uintptr_t kDbLoadFastfilesRVA = 0x11ACD30;
    constexpr uint8_t kDbLoadFastfilesPrologue[] = {
        0x40, 0x55, 0x48, 0x83, 0xEC, 0x40,
        0x48, 0x89, 0x5C, 0x24, 0x50,
        0x48, 0x89, 0x74, 0x24, 0x58,
        0x48, 0x89, 0x7C, 0x24, 0x60,
    };
    constexpr size_t kDbLoadFastfilesStolen = sizeof(kDbLoadFastfilesPrologue);

    // The target database names this as luaL_loadfile_FastFile. The first 23 bytes
    // end at an instruction boundary. Use replacement chunks only. Do not call Lua from C.
    constexpr uintptr_t kLuaLoadfileFastfileRVA = 0x19BAEE0;
    constexpr uint8_t kLuaLoadfileFastfilePrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x6C, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x18,
        0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA,
    };
    constexpr size_t kLuaLoadfileFastfileStolen = sizeof(kLuaLoadfileFastfilePrologue);

    // This function registers an Engine Lua function. It runs during engine Lua setup.
    // It is an address record only until the target registration owner is confirmed.
    constexpr uintptr_t kLuiRegisterFunctionRVA = 0x19CDC60;
    constexpr uint8_t kLuiRegisterFunctionPrologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
        0x0F, 0xB6, 0x15, 0x42, 0xAC, 0xE9, 0x0F,
        0x8B, 0xD9,
    };
    constexpr size_t kLuiRegisterFunctionStolen = sizeof(kLuiRegisterFunctionPrologue);

    // The target string is "MatchRules.SetMatchRulesData called while not system link host".
    constexpr uintptr_t kSystemLinkSetMatchRulesRVA = 0x19F25D0;
    constexpr uint8_t kSystemLinkSetMatchRulesPrologue[] = {
        0x48, 0x89, 0x6C, 0x24, 0x20, 0x56,
        0x48, 0x83, 0xEC, 0x60,
        0x48, 0x8B, 0x05, 0x17, 0x42, 0x99, 0x0B,
    };
    constexpr size_t kSystemLinkSetMatchRulesStolen = sizeof(kSystemLinkSetMatchRulesPrologue);

    // The target database gives this per-frame match-rules callback.
    constexpr uintptr_t kMatchRulesFrameRVA = 0x1040E50;
    constexpr uint8_t kMatchRulesFramePrologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8D, 0x15, 0x23, 0x56, 0x75, 0x0B,
        0x48, 0x8B, 0xD9,
    };
    constexpr size_t kMatchRulesFrameStolen = sizeof(kMatchRulesFramePrologue);

    // FS_FOpenFileReadCurrentThread(filename, fileHandle). The cleaned game-test source
    // confirms that this wrapper sends the requested qpath to FS_FOpenFileReadForThread.
    // The target database confirms this RVA and the 16-byte boundary below. This proxy uses
    // it only as an opt-in request monitor. It does not redirect a zone path by default.
    constexpr uintptr_t kFsFOpenFileReadCurrentThreadRVA = 0x13D9240;
    constexpr uint8_t kFsFOpenFileReadCurrentThreadPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x57,
        0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0xDA,
        0x48, 0x8B, 0xF9,
    };
    constexpr size_t kFsFOpenFileReadCurrentThreadStolen =
        sizeof(kFsFOpenFileReadCurrentThreadPrologue);

    // DB_LoadZone accepts a zone name. It is not a substitute for the System Link map path.
    constexpr uintptr_t kDbLoadZoneRVA = 0x12279C0;

    // This target function reports bad fastfile data header sentinels.
    constexpr uintptr_t kDbAuthLoadInflateInternalRVA = 0xD89E10;
    constexpr uint8_t kDbAuthLoadInflateInternalPrologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
        0x44, 0x8B, 0x81, 0x18, 0xE9, 0x08, 0x00,
        0x48, 0x8B, 0xD9, 0x45, 0x85, 0xC0, 0x75, 0x57,
    };
    constexpr size_t kDbAuthLoadInflateInternalStolen = sizeof(kDbAuthLoadInflateInternalPrologue);

    // These FS helpers are map-exists candidates. Do not use them as a global bypass.
    constexpr uintptr_t kFsFileExistsRVA = 0x16B2630;
    constexpr uintptr_t kFsFileExistsWithPathRVA = 0x16B26A0;

    // This GSC helper is not the menu map validator.
    constexpr uintptr_t kGscrMapExistsRVA = 0x15E16B0;

    // LoadTables and ClearTables are now independently verified in replay_bindings.h.
    constexpr uintptr_t kOmnvarRegisterFlagsFromStringTableRVA = 0xCD67C0;
}
