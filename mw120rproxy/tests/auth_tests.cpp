#include "offline_auth.h"
#include "offline_identity.h"
#include "log.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "auth_decoder_fixture.h"

namespace
{
    void Check(bool condition, const char* text)
    {
        if (!condition) { fprintf(stderr, "FAIL: %s\n", text); std::exit(1); }
    }
    uint8_t* image;
    constexpr uintptr_t auth = 0x4622910, user = 0xE5C0730;
    void Commit(uintptr_t rva)
    {
        Check(VirtualAlloc(image + (rva & ~uintptr_t{0xFFF}), 0x1000, MEM_COMMIT,
            PAGE_EXECUTE_READWRITE) != nullptr, "commit fixture page");
    }
    void Bytes(uintptr_t rva, std::initializer_list<uint8_t> bytes)
    {
        memcpy(image + rva, bytes.begin(), bytes.size());
    }
    template<class T> T& At(uintptr_t rva) { return *reinterpret_cast<T*>(image + rva); }

    void DecoderTest()
    {
        // The decoder body is extracted from Replay's OwnsBaseGame callback,
        // not another implementation of the C++ encoder under test.
        auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        Check(code != nullptr, "allocate native decoder fixture");
        const uint8_t prefix[] = {0x48,0x83,0xEC,0x58,0x48,0x8B,0xC1}; // stack scratch; rax=object
        memcpy(code, prefix, sizeof(prefix));
        memcpy(code + sizeof(prefix), kNativeOwnershipDecoder, sizeof(kNativeOwnershipDecoder));
        const uint8_t tail[] = {0x41,0x8B,0xC0,0x48,0x83,0xC4,0x58,0xC3}; // return r8d
        memcpy(code + sizeof(prefix) + sizeof(kNativeOwnershipDecoder), tail, sizeof(tail));
        FlushInstructionCache(GetCurrentProcess(), code, 4096);
        using DecodeFn = uint32_t(*)(void*);
        auto decode = reinterpret_cast<DecodeFn>(code);
        alignas(16) uint8_t object[0x330]{};
        for (unsigned offset = 0; offset < 16; ++offset)
        {
            auto* field = object + offset + 0x2F4;
            for (uint32_t key : {0u, 1u, 0x12345678u, 0xFFFFFFFFu})
                for (uint8_t shift = 0; shift < 32; ++shift)
                    for (bool value : {false, true})
                    {
                        const auto encoded = offlineauth::TransformOwnership(value ? uint32_t{1} << shift : 0,
                            reinterpret_cast<uintptr_t>(field), key);
                        memcpy(field, &encoded, 4);
                        field[4] = shift;
                        memcpy(field + 8, &key, 4);
                        Check(decode(object + offset) == unsigned(value), "native decoder agrees for address/key/shift/value");
                    }
        }
        VirtualFree(code, 0, MEM_RELEASE);
        puts("PASS: 4,096 ownership encodings executed through exact Replay decoder instructions");
    }

    void HookTest()
    {
        image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1324B000, MEM_RESERVE, PAGE_NOACCESS));
        Check(image != nullptr, "reserve sparse Replay fixture");
        for (uintptr_t rva : {0x165F000,0x1660000,0x1662000,0x1664000,0x1665000,0x1666000,0x1528000,
             0x17EC000,0x12A1000,0x259C000,0x1A03000,0x5A54000,0xEE55000,
             0x4622000,0xE5C0000,0xF05A000,0xE371000,0xD317000}) Commit(rva);

        Bytes(0x1660280,{0x48,0x8D,0x05,0x89,0x26,0xFC,0x02,0xC3});
        Bytes(0x1662FC0,{0x48,0x8D,0x05,0x49,0x7C,0x9F,0x0D,0xC3});
        Bytes(0x1662C40,{0x48,0x89,0x5C,0x24,8,0x48,0x89,0x7C,0x24,0x10,0x55,0x48,0x8B,0xEC,
            0xB0,1,0x5D,0xC3}); // exact native entry, harmless stand-in connection success
        Bytes(0x1665990,{0x48,0x63,0xC1,0x48,0x69,0xC8,0xE8,0,0,0,0x48,0x8D,0x05,0x8F,0xAD,0xF5,0x0C});
        Bytes(0x1665C10,{0x48,0x8B,0x05,0xD1,0x50,0x9F,0x0D,0x48,0x89,0x01,0x48,0x8B,0xC1,0xC3});
        Bytes(0x1A039E0,{0x40,0x53,0x48,0x83,0xEC,0x20,0xBA,1,0,0,0,0x48,0x8B,0xD9});
        Bytes(0x165FFA0,{0x40,0x53,0x48,0x83,0xEC,0x50,0x48,0x8B,0x05,0x4B,0x44,0x3F,0x04,
            0x83,0x39,0,0x75,6,0xC7,1,3,0,0,0, // native-like state=0 failure; state=2 returns
            0x48,0x89,0x51,0x10,0x4C,0x89,0x41,0x18,0x4C,0x89,0x49,0x20, // original arg forwarding
            0x48,0x83,0xC4,0x50,0x5B,0xC3});
        Bytes(0x1528490,{0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xD9,
            0x33,0xC0,0x48,0x83,0xC4,0x20,0x5B,0xC3});
        Bytes(0x17EC930,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x63,0xC1,0xBA,0x21,0,0,0,
            0xB8,7,0,0,0,0x48,0x83,0xC4,0x20,0x5B,0xC3});
        Bytes(0x12A1EB0,{0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x65,0x3A,0xBB,0x0D,
            0x33,0xC0,0x48,0x83,0xC4,0x28,0xC3});
        Bytes(0x1666030,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,
            0x48,0xC7,3,123,0,0,0,0xB0,1,0x48,0x83,0xC4,0x20,0x5B,0xC3});
        Bytes(0x1665C80,{0x48,0x89,0x5C,0x24,8,0x57,0x48,0x83,0xEC,0x20,
            0xC6,2,'S',0xC6,0x42,1,0,0xB0,1,0x48,0x83,0xC4,0x20,0x5F,0x48,0x8B,0x5C,0x24,8,0xC3});
        Bytes(0x16649F0,{0x40,0x53,0x48,0x81,0xEC,0xB0,0,0,0,0x48,0x8B,0x05,0xF8,0xF9,0x3E,0x04,
            0x48,0x8B,0xC1,0x48,0x81,0xC4,0xB0,0,0,0,0x5B,0xC3});
        FlushInstructionCache(GetCurrentProcess(), image, 0x1324B000);

        auto begin = reinterpret_cast<void(*)(void*, uintptr_t, uintptr_t, uintptr_t)>(image + 0x165FFA0);
        auto signedIn = reinterpret_cast<bool(*)(int)>(image + 0x1528490);
        auto status = reinterpret_cast<int(*)(int)>(image + 0x17EC930);
        auto stats = reinterpret_cast<int(*)(int)>(image + 0x12A1EB0);
        auto platform = reinterpret_cast<bool(*)(int,uint64_t*)>(image + 0x1666030);
        auto name = reinterpret_cast<bool(*)(int,char*,size_t)>(image + 0x1665C80);
        auto error = reinterpret_cast<const char*(*)(const uint32_t*)>(image + 0x16649F0);
        auto connect = reinterpret_cast<bool(*)(void*)>(image + 0x1662C40);
        Check(offlineauth::Prepare(GetModuleHandleW(nullptr)), "persistent test identity created/loaded");
        Check(offlineauth::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::NotReady, "mismatched registration blocks auth activation");
        begin(image + auth, 11, 22, 33);
        Check(At<int>(auth) == 3 && At<int>(user) == 0, "partial installation forwards stock without writing identity");
        Check(!signedIn(0) && status(0) == 7 && stats(0) == 0, "no premature local success");
        Check(connect(image+0xF05AC10), "disabled/partial installation forwards platform connection");

        At<uintptr_t>(0x259C448) = reinterpret_cast<uintptr_t>(image + 0x19B96A0);
        Check(offlineauth::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed, "retry completes all hooks");
        Check(At<uintptr_t>(0x259C448) == reinterpret_cast<uintptr_t>(image + 0x1A039E0), "stock game-owned offline callback selected");
        alignas(8) uint8_t other[0x318]{};
        Check(!connect(image+0xF05AC10) && connect(other), "only verified offline platform singleton skips connection");
        Check(At<int>(user)==0, "skipping platform connection does not prematurely publish auth state");
        begin(other, 44, 55, 66);
        Check(*reinterpret_cast<int*>(other) == 3 && At<int>(user) == 0, "unexpected auth object forwarded without state writes");
        memset(image + user, 0xCC, sizeof(offlineauth::UserRecord) * 2);
        At<uint32_t>(auth + 0x2FC) = 0x3A71E509;
        At<uint8_t>(auth + 0x2F8) = 29;
        At<uint32_t>(0xE371232) = 0x12345678;
        At<uint8_t>(0xD317DB4) = 0;
        At<uint32_t>(0xF05AD24) = 0x7FFFFF01;
        begin(image + auth, 77, 88, 99);
        auto& record = At<offlineauth::UserRecord>(user);
        const auto firstId = record.xuid;
        Check(At<int>(auth) == 2 && record.signInState == 2, "auth and user agree on sign-in");
        Check(firstId != 0 && record.platformId == firstId && At<uint64_t>(0xF05ACE8) == firstId, "all numeric identities match");
        Check(strtoull(record.xuidText, nullptr, 10) == firstId && strcmp(record.xuidText, record.platformIdText) == 0, "numeric and string identities match");
        Check(strcmp(record.name, record.platformName) == 0 && strcmp(record.name, record.fullName) == 0, "all account names match");
        Check(At<uint32_t>(user + 0x28) == 0xCCCCCCCC && At<uint32_t>(user + 0xE8) == 0xCCCCCCCC, "reserved bytes and controller 1 untouched");
        Check(At<uint8_t>(0xE371231) == 1 && At<uint32_t>(0xE371232) == 0x12345678, "content flag uses a one-byte write");
        Check(At<uint8_t>(0xD317DB4) == 0, "unfetched stats are not reported as fetched");
        Check(At<uint64_t>(auth + 0x10) == 77 && At<uint64_t>(auth + 0x18) == 88 && At<uint64_t>(auth + 0x20) == 99, "stock auth call receives original arguments");
        Check(signedIn(0) && status(0) == 2 && stats(0) == 1, "controller 0 local status and stats source");
        Check(!signedIn(1) && status(1) == 7 && stats(1) == 0, "other controllers retain stock status");
        uint64_t id = 0;
        Check(platform(0, &id) && id == firstId, "platform getter uses the same 64-bit identity");
        Check(platform(1, &id) && id == 123, "other platform IDs forwarded");
        Check(!platform(0, nullptr), "invalid ID output rejected");
        char output[36]{};
        Check(name(0, output, sizeof(output)) && strcmp(output, record.name) == 0, "platform getter uses the same name");
        Check(name(1, output, sizeof(output)) && strcmp(output, "S") == 0, "other platform names forwarded");
        Check(!name(0, nullptr, 36) && !name(0, output, 0), "invalid name outputs rejected");
        uint32_t code = 0x7FFFFF01;
        Check(error(&code) == reinterpret_cast<char*>(&code), "Blizzard error formatter is observed and still forwarded");
        At<int>(auth) = 3;
        At<uint64_t>(user + 0xB8) = 0;
        begin(image + auth, 1, 2, 3);
        Check(record.platformId == firstId && At<int>(auth) == 2, "backend reset resynchronizes the existing local identity");
        Check(offlineauth::Prepare(GetModuleHandleW(nullptr)), "identity reload succeeds");
        At<uint64_t>(user + 0x90) = 0;
        begin(image + auth, 1, 2, 3);
        Check(record.xuid == firstId, "identity persists across preparation");
        Check(offlineauth::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed, "installation is idempotent");
        offlineauth::LogSnapshot();
        puts("PASS: auth hook retries, readiness gating, controller isolation, native forwarding, identity persistence and consistency, reset recovery");
        // Installed trampolines and sparse image intentionally have process lifetime.
    }
}

int main()
{
    log120r::Init(GetModuleHandleW(nullptr), false);
    DecoderTest();
    HookTest();
    return 0;
}
