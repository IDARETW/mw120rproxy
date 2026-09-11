#pragma once
// -----------------------------------------------------------------------------
// Compile-time string obfuscation for mw120rproxy.
//
// Wrapping a string literal in OBF("...") stores it XOR-encrypted in the binary's
// .rdata (so a `strings` dump / static disassembly can't read it) and decrypts it
// into a stack buffer at runtime, on demand. The result is usable anywhere a
// `const char*` is (implicit conversion) or via .c_str().
//
//   LOG_INFO(...)                         -> format strings obfuscated at the macro
//                                            boundary (log.h), zero call-site churn.
//   OBF("literal")                        -> a decrypting temporary (const char*).
//   OBF("literal").c_str()                -> explicit const char* (needed in a `...`
//                                            vararg slot, where implicit conversion
//                                            of a class type does NOT fire).
//
// Design notes:
//  * Per-call-site keystream: __COUNTER__ and __LINE__ mixed with a build salt, so
//    identical strings at different sites encrypt differently.
//  * The decrypt loop reads the ciphertext through a `volatile` load. That is the
//    guard that stops the optimizer (esp. MSVC /GL LTCG) from const-folding the
//    decrypt back to plaintext and defeating the whole point.
//  * KeyByte() must stay a pure compile-time function (encrypt and decrypt share it);
//    do NOT make it depend on any runtime value.
//
// ⚠️ VERIFY AFTER BUILDING: the plaintext must be ABSENT from the compiled DLL.
//    (grep the binary — see the header's companion test.) Never trust obfuscation
//    you haven't confirmed on the actual bytes.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

// Per-SHIP-build salt (not wired up yet — no Ship config exists for mw120rproxy). Kept for source
// parity with the other proxies so this file drops in unchanged if/when a Ship config is added.
#ifdef MW120R_SHIP
#include "build_salt.h"
#ifndef MW120R_BUILD_SALT
#define MW120R_BUILD_SALT 0ULL
#endif
#endif

namespace strobf {
// Build salt. Base value is fixed (bump it to re-key every obfuscated string); a SHIP build additionally
// XORs in a fresh per-build salt (build_salt.h) so shipped DLLs don't share a keystream.
#ifdef MW120R_SHIP
inline constexpr uint64_t kSalt = 0xA5F1C3D7B92E6041ULL ^ (uint64_t)(MW120R_BUILD_SALT);
#else
inline constexpr uint64_t kSalt = 0xA5F1C3D7B92E6041ULL;
#endif

// Pure compile-time keystream byte (splitmix64 mixer). Never returns 0 — an XOR
// with 0 would leave that plaintext byte in the clear.
constexpr uint8_t KeyByte(uint64_t seed, size_t i) {
    uint64_t x = seed + kSalt + (uint64_t)i * 0x9E3779B97F4A7C15ULL;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    uint8_t k = (uint8_t)(x & 0xFF);
    return k ? k : 0x5A;
}

// Ciphertext, materialized at compile time in .rdata (never the plaintext).
template <size_t N, uint64_t Seed> struct Encrypted {
    char data[N];
    constexpr Encrypted(const char (&s)[N]) : data{} {
        for (size_t i = 0; i < N; ++i)
            data[i] = (char)((uint8_t)s[i] ^ KeyByte(Seed, i));
    }
};

// Runtime decrypt into a local buffer. Lives for the full expression it appears in,
// which is all a format string / transient const char* needs.
template <size_t N, uint64_t Seed> struct Decryptor {
    char buf[N];
    __forceinline Decryptor(const Encrypted<N, Seed>& e) : buf{} {
        for (size_t i = 0; i < N; ++i) {
            volatile uint8_t c = (uint8_t)e.data[i]; // volatile load blocks const-folding
            buf[i] = (char)(c ^ KeyByte(Seed, i));
        }
    }
    __forceinline operator const char*() const {
        return buf;
    }
    __forceinline const char* c_str() const {
        return buf;
    }
};
}

namespace strobf {
// 16-bit keystream unit for wide strings (never 0 — an XOR-0 would leak the plaintext unit).
constexpr uint16_t KeyWord(uint64_t seed, size_t i) {
    uint64_t x = seed + kSalt + (uint64_t)i * 0xD1B54A32D192ED03ULL;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    uint16_t k = (uint16_t)(x & 0xFFFF);
    return k ? k : 0x5A5A;
}

template <size_t N, uint64_t Seed> struct EncryptedW {
    wchar_t data[N];
    constexpr EncryptedW(const wchar_t (&s)[N]) : data{} {
        for (size_t i = 0; i < N; ++i)
            data[i] = (wchar_t)((uint16_t)s[i] ^ KeyWord(Seed, i));
    }
};

template <size_t N, uint64_t Seed> struct DecryptorW {
    wchar_t buf[N];
    __forceinline DecryptorW(const EncryptedW<N, Seed>& e) : buf{} {
        for (size_t i = 0; i < N; ++i) {
            volatile uint16_t c = (uint16_t)e.data[i]; // volatile load blocks const-folding
            buf[i] = (wchar_t)(c ^ KeyWord(Seed, i));
        }
    }
    __forceinline operator const wchar_t*() const {
        return buf;
    }
    __forceinline const wchar_t* c_str() const {
        return buf;
    }
};
}

namespace strobf {
// Case-insensitive FNV-1a32. Used to compare a runtime label against compile-time-hashed literals so
// the literals never reach .rdata (see FNVI). Runtime + compile-time capable.
constexpr uint32_t Fnv32i(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) {
        char ch = *s;
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch + 32);
        h ^= (uint8_t)ch;
        h *= 16777619u;
    }
    return h;
}
// consteval wrapper forces the literal to be hashed at compile time (so it is not emitted).
template <size_t N> consteval uint32_t Fnv32iLit(const char (&s)[N]) {
    return Fnv32i(s);
}
}
// Compile-time case-insensitive hash of a string literal (the literal is folded away, never stored).
#define FNVI(s) ::strobf::Fnv32iLit(s)

// Unique-ish per-site seed. __COUNTER__ spreads within a TU; __LINE__ spreads across TUs.
#define OBF_SEED                                                                                   \
    ((uint64_t)(__COUNTER__) ^ ((uint64_t)(__LINE__) * 0x100000001B3ULL) ^ ::strobf::kSalt)

#define OBF_IMPL(s, seed)                                                                          \
    ([]()->::strobf::Decryptor<sizeof(s), (seed)> {                                                \
        constexpr ::strobf::Encrypted<sizeof(s), (seed)> _e(s);                                    \
        return ::strobf::Decryptor<sizeof(s), (seed)>(_e);                                         \
    }())

// Obfuscate a string literal. Usable as const char* (implicit) or via .c_str().
#define OBF(s) OBF_IMPL(s, OBF_SEED)

// OBFS — persistent obfuscated string: decrypts ONCE into a function-local static buffer and returns a
// STABLE `const char*` (safe to STORE — unlike OBF's per-full-expression temporary). Use for a literal
// whose pointer outlives the expression: kept in a struct/array and logged later (e.g. the code_patch /
// fence_patch plan[].label). Same static-analysis resistance as OBF (ciphertext in .rdata; a runtime dump
// sees the decrypted buffer, exactly like OBF). One static buffer per call site; first-call decrypt is
// magic-static thread-safe.
#define OBFS_IMPL(s, seed)                                                                         \
    ([]() -> const char* {                                                                         \
        static constexpr ::strobf::Encrypted<sizeof(s), (seed)> _e(s);                             \
        static char _buf[sizeof(s)];                                                               \
        static const bool _once = []() -> bool {                                                   \
            for (size_t i = 0; i < sizeof(s); ++i) {                                               \
                volatile uint8_t c = (uint8_t)_e.data[i];                                          \
                _buf[i] = (char)(c ^ ::strobf::KeyByte((seed), i));                                \
            }                                                                                      \
            return true;                                                                           \
        }();                                                                                       \
        (void)_once;                                                                               \
        return _buf;                                                                               \
    }())
#define OBFS(s) OBFS_IMPL(s, OBF_SEED)

#define OBFW_IMPL(s, seed)                                                                         \
    ([]()->::strobf::DecryptorW<sizeof(s) / sizeof(wchar_t), (seed)> {                             \
        constexpr ::strobf::EncryptedW<sizeof(s) / sizeof(wchar_t), (seed)> _e(s);                 \
        return ::strobf::DecryptorW<sizeof(s) / sizeof(wchar_t), (seed)>(_e);                      \
    }())

// Obfuscate a WIDE string literal (L"..."). Usable as const wchar_t* (implicit) or via .c_str().
#define OBFW(s) OBFW_IMPL(s, OBF_SEED)
