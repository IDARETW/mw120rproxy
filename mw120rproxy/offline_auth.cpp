#include "offline_auth.h"
#include "offline_identity.h"
#include "safemem.h"
#include "logger.h"
#include <bcrypt.h>
#include <atomic>
#include <array>
#include <charconv>
#include <intrin.h>

namespace offlineauth
{
    namespace
    {
        constexpr uintptr_t kUser = 0xE5C0730;
        constexpr uintptr_t kAuth = 0x4622910;
        constexpr uintptr_t kPlatformClient = 0xF05AC10;
        constexpr uintptr_t kPlatformXuid = 0xF05ACE8;
        constexpr uintptr_t kDevName = 0xF05AD00;
        constexpr uintptr_t kContentFinished = 0xE371231;
        constexpr uintptr_t kLastBnetError = 0xF05AD24;
        constexpr uintptr_t kOfflineStatsFetched = 0xD317DB4;
        constexpr uintptr_t kDemoRegistration = 0x259C448;
        constexpr uintptr_t kStockDemo = 0x19B96A0;
        constexpr uintptr_t kStockTrue = 0x1A039E0;
        uintptr_t g_base = 0;
        Identity g_identity;
        bool g_prepared = false;
        std::atomic<bool> g_enabled{false}, g_ready{false};
        std::atomic<unsigned long long> g_beginCalls{0}, g_dwCalls{0}, g_statsCalls{0};
        std::atomic<unsigned> g_repairs{0}, g_errorCalls{0};
        using BeginFn = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
        using SignedFn = bool(*)(int);
        using StatusFn = int(*)(int);
        using PlatformIdFn = bool(*)(int, uint64_t*);
        using PlatformNameFn = bool(*)(int, char*, size_t);
        using ErrorFn = const char*(*)(const uint32_t*);
        using ConnectFn = bool(*)(uintptr_t);
        std::atomic<ConnectFn> g_connect{nullptr};
        std::atomic<unsigned> g_connectSkipped{0};
        std::atomic<BeginFn> g_begin{nullptr};
        std::atomic<SignedFn> g_signed{nullptr};
        std::atomic<StatusFn> g_status{nullptr}, g_stats{nullptr};
        std::atomic<PlatformIdFn> g_platformId{nullptr};
        std::atomic<PlatformNameFn> g_platformName{nullptr};
        std::atomic<ErrorFn> g_error{nullptr};

        bool Writable(uintptr_t address, size_t size)
        {
            size_t available = 0;
            return safemem::RegionReadable(reinterpret_cast<void*>(address), available) &&
                   available >= size && safemem::RegionWritable(reinterpret_cast<void*>(address));
        }
        template<class T> T Read(uintptr_t rva)
        {
            T result{};
            safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &result, sizeof(result));
            return result;
        }
        template<class T> void Write(uintptr_t rva, T value)
        {
            // All ranges are checked together before the first write.
            memcpy(reinterpret_cast<void*>(g_base + rva), &value, sizeof(value));
        }
        bool Ready(int controller)
        {
            return controller == 0 && g_ready.load(std::memory_order_acquire);
        }

        bool CompleteLocalAuth(uintptr_t object)
        {
            if (object != g_base + kAuth) return false;
            const std::pair<uintptr_t, size_t> regions[] = {
                {kAuth, 0x318}, {kUser, sizeof(UserRecord)}, {kPlatformXuid, 8},
                {kDevName, 36}, {kContentFinished, 1}, {kLastBnetError, 4}};
            for (auto [rva, size] : regions)
                if (!Writable(g_base + rva, size)) return false;
            const int previous = Read<int>(kAuth);
            const uint32_t previousError = Read<uint32_t>(kLastBnetError);
            const bool first = !g_ready.load(std::memory_order_relaxed);
            const bool changed = first || previous != 2 || Read<int>(kUser) != 2 ||
                Read<uint64_t>(kUser + 0x90) != g_identity.xuid ||
                Read<uint64_t>(kUser + 0xB8) != g_identity.xuid ||
                Read<uint64_t>(kPlatformXuid) != g_identity.xuid ||
                Read<uint8_t>(kAuth + 0x2D0) != 1;
            if (!changed && previousError == 0) return true;

            g_ready.store(false, std::memory_order_release);
            PopulateUser(*reinterpret_cast<UserRecord*>(g_base + kUser), g_identity);
            Write<uint64_t>(kPlatformXuid, g_identity.xuid);
            memcpy(reinterpret_cast<void*>(g_base + kDevName), g_identity.name, sizeof(g_identity.name));
            // kUser+0xB8 is also the platform/presence ID. Do not write xuid/6 here:
            // zeroproxy's subsequent assign_xuid overwrites that earlier write too.
            const uintptr_t field = kAuth + 0x2F4;
            // Preserve the game's initialized per-field key and shift.
            const uint32_t key = Read<uint32_t>(field + 8);
            const uint8_t shift = Read<uint8_t>(field + 4) & 31u;
            Write<uint32_t>(field, TransformOwnership(uint32_t{1} << shift, g_base + field, key));
            Write<uint8_t>(field + 4, shift);
            Write<uint8_t>(kAuth + 0x2D0, 1);
            Write<uint32_t>(kAuth + 0x1C8, 0); // auth failure result
            Write<uint8_t>(kAuth + 0x314, 0); // disconnected-after-auth flag
            Write<uint32_t>(kLastBnetError, 0);
            Write<uint8_t>(kContentFinished, 1); // byte in Replay, never a DWORD
            Write<int32_t>(kAuth, 2);
            g_ready.store(true, std::memory_order_release);
            if (g_repairs.fetch_add(1) < 8)
                LOG_INFO("Auth", "%s local controller 0: bnet=%d->2 prior_error=%08X xuid=%llu name='%s' ownership=1",
                    first ? "initialized" : "resynchronized", previous, previousError,
                    static_cast<unsigned long long>(g_identity.xuid), g_identity.name);
            return true;
        }

        bool ConnectPlatformClient(uintptr_t object)
        {
            // Live frame 1665AC0 calls platform update 1662F00 BEFORE BeginAuth.
            // Its 1662C40 connection attempt creates the external Battle.net
            // client and calls Connect at 3E4FA0. An absent launcher can block
            // this frame for ~30 seconds, so BeginAuth cannot run until timeout.
            // Offline mode needs no external client. Return a real disconnected
            // result; stock retry bookkeeping and the following local auth run.
            if (g_enabled.load(std::memory_order_acquire) && object == g_base + kPlatformClient)
            {
                if (g_connectSkipped.fetch_add(1) == 0)
                    LOG_INFO("Auth", "offline startup: skipped external Battle.net client connection before BeginAuth");
                return false;
            }
            return g_connect.load(std::memory_order_acquire)(object);
        }

        void Begin(uintptr_t object, uintptr_t a2, uintptr_t a3, uintptr_t a4)
        {
            g_beginCalls.fetch_add(1, std::memory_order_relaxed);
            if (g_enabled.load(std::memory_order_acquire))
            {
                if (!CompleteLocalAuth(object))
                {
                    g_ready.store(false, std::memory_order_release);
                    if (g_repairs.fetch_add(1) < 8)
                        LOG_ERR("Auth", "BeginAuth object/ranges do not match Replay; forwarding stock call");
                }
            }
            // With state=2, stock BeginAuth takes its existing no-work branch.
            // Preserve its calling convention and never feed it invented SSO tokens.
            g_begin.load(std::memory_order_acquire)(object, a2, a3, a4);
        }
        bool Signed(int controller)
        {
            ++g_dwCalls;
            return Ready(controller) ? true : g_signed.load(std::memory_order_acquire)(controller);
        }
        int Status(int controller)
        {
            return Ready(controller) ? 2 : g_status.load(std::memory_order_acquire)(controller);
        }
        int Stats(int controller)
        {
            ++g_statsCalls;
            return Ready(controller) ? 1 : g_stats.load(std::memory_order_acquire)(controller);
        }
        bool PlatformId(int controller, uint64_t* out)
        {
            if (!Ready(controller)) return g_platformId.load(std::memory_order_acquire)(controller, out);
            return safemem::WriteBytes(out, &g_identity.xuid, sizeof(g_identity.xuid));
        }
        bool PlatformName(int controller, char* out, size_t size)
        {
            if (!Ready(controller)) return g_platformName.load(std::memory_order_acquire)(controller, out, size);
            if (!size || size > 4096 || !Writable(reinterpret_cast<uintptr_t>(out), size)) return false;
            strncpy_s(out, size, g_identity.name, _TRUNCATE);
            return true;
        }
        const char* Error(const uint32_t* code)
        {
            if (g_errorCalls.fetch_add(1) < 8)
            {
                uint32_t value = 0;
                safemem::ReadBytes(code, &value, sizeof(value));
                LOG_ERR("Auth", "native Blizzard error BLZBNTBGS%08X; local_ready=%d", value, int(g_ready.load()));
                void* frames[12]{};
                const USHORT count = RtlCaptureStackBackTrace(1, 12, frames, nullptr);
                for (USHORT i = 0; i < count; ++i)
                {
                    const uintptr_t pc = reinterpret_cast<uintptr_t>(frames[i]);
                    if (pc >= g_base && pc < g_base + 0x1324B000)
                        LOG_ERR("Auth", "  frame %u game RVA 0x%llX", i, static_cast<unsigned long long>(pc - g_base));
                }
                LogSnapshot();
            }
            return g_error.load(std::memory_order_acquire)(code);
        }

        template<size_t N> bool Matches(uintptr_t rva, const uint8_t (&bytes)[N])
        {
            std::array<uint8_t, N> actual{};
            return safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), actual.data(), N) &&
                   memcmp(actual.data(), bytes, N) == 0;
        }
        hook::Status UseStockOfflineBootCallback()
        {
            const uint8_t trueCode[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0xBA,1,0,0,0,0x48,0x8B,0xD9};
            if (!Matches(kStockTrue, trueCode)) return hook::Status::NotReady;
            auto** slot = reinterpret_cast<void**>(g_base + kDemoRegistration);
            void* expected = reinterpret_cast<void*>(g_base + kStockDemo);
            void* replacement = reinterpret_cast<void*>(g_base + kStockTrue);
            const auto current = Read<uintptr_t>(kDemoRegistration);
            if (current == reinterpret_cast<uintptr_t>(replacement)) return hook::Status::Installed;
            if (current != reinterpret_cast<uintptr_t>(expected)) return hook::Status::NotReady;
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return hook::Status::Failed;
            void* prior = InterlockedCompareExchangePointer(slot, replacement, expected);
            DWORD ignored = 0;
            const bool restored = VirtualProtect(slot, sizeof(void*), old, &ignored) != FALSE;
            if (prior != expected || !restored) return hook::Status::Failed;
            LOG_INFO("Auth", "offline boot: Engine.IsDemoBuild uses stock true callback; Local Play already allowed in Replay");
            return hook::Status::Installed;
        }
    }

    bool Prepare(HMODULE self)
    {
        wchar_t path[MAX_PATH]{};
        const DWORD len = GetModuleFileNameW(self, path, MAX_PATH);
        if (!len || len >= MAX_PATH) return false;
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash) return false;
        if (wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"mw120rproxy.identity.ini")) return false;
        const DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) return false;
            uint64_t random = 0;
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&random), sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                return false;
            const uint64_t id = 0x11CB1243B8D7C31Eull | (random * random);
            char text[160]{};
            const int count = sprintf_s(text, "[local_identity]\r\nxuid=%llu\r\nname=LocalPlayer\r\n",
                static_cast<unsigned long long>(id));
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return false;
            DWORD written = 0;
            const bool saved = WriteFile(file, text, count, &written, nullptr) && written == static_cast<DWORD>(count) && FlushFileBuffers(file);
            CloseHandle(file);
            if (!saved) return false;
        }
        wchar_t idText[32]{}, name[36]{};
        GetPrivateProfileStringW(L"local_identity", L"xuid", L"", idText, 32, path);
        char digits[32]{};
        for (size_t i = 0; idText[i]; ++i)
        {
            if (idText[i] < L'0' || idText[i] > L'9') return false;
            digits[i] = static_cast<char>(idText[i]);
        }
        const auto parsed = std::from_chars(digits, digits + strlen(digits), g_identity.xuid);
        if (parsed.ec != std::errc{} || parsed.ptr != digits + strlen(digits) || !g_identity.xuid) return false;
        GetPrivateProfileStringW(L"local_identity", L"name", L"LocalPlayer", name, 36, path);
        // Bounded ASCII keeps the 36-byte engine names valid; no truncated UTF-8.
        for (size_t i = 0; name[i]; ++i)
            g_identity.name[i] = name[i] >= 32 && name[i] <= 126 ? static_cast<char>(name[i]) : '_';
        if (!g_identity.name[0]) strcpy_s(g_identity.name, "LocalPlayer");
        g_prepared = true;
        LOG_INFO("Auth", "persistent local identity loaded; xuid=%llu name='%s' (local MP compatibility)",
            static_cast<unsigned long long>(g_identity.xuid), g_identity.name);
        return true;
    }

    hook::Status Install(uintptr_t base)
    {
        if (!g_prepared) return hook::Status::Failed;
        if (g_enabled.load()) return hook::Status::Installed;
        g_base = base;
        // Independent anchors prove the data addresses and v1 record stride.
        const uint8_t getter[] = {0x48,0x8D,0x05,0x89,0x26,0xFC,0x02,0xC3};
        const uint8_t userGetter[] = {0x48,0x63,0xC1,0x48,0x69,0xC8,0xE8,0,0,0,0x48,0x8D,0x05,0x8F,0xAD,0xF5,0x0C};
        const uint8_t xuidGetter[] = {0x48,0x8B,0x05,0xD1,0x50,0x9F,0x0D,0x48,0x89,0x01,0x48,0x8B,0xC1,0xC3};
        const uint8_t clientGetter[] = {0x48,0x8D,0x05,0x49,0x7C,0x9F,0x0D,0xC3};
        if (!Matches(0x1660280, getter) || !Matches(0x1665990, userGetter) || !Matches(0x1665C10, xuidGetter) || !Matches(0x1662FC0,clientGetter))
            return hook::Status::NotReady;

        // Complete instruction boundaries verified in the MD5-matching Replay image.
        const uint8_t begin[] = {0x40,0x53,0x48,0x83,0xEC,0x50,0x48,0x8B,0x05,0x4B,0x44,0x3F,0x04};
        const uint8_t signedIn[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xD9};
        const uint8_t status[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x63,0xC1,0xBA,0x21,0,0,0};
        const uint8_t stats[] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x65,0x3A,0xBB,0x0D};
        const uint8_t platformId[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA};
        const uint8_t platformName[] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20};
        const uint8_t error[] = {0x40,0x53,0x48,0x81,0xEC,0xB0,0,0,0,0x48,0x8B,0x05,0xF8,0xF9,0x3E,0x04};
        const uint8_t connect[] = {0x48,0x89,0x5C,0x24,8,0x48,0x89,0x7C,0x24,0x10,0x55,0x48,0x8B,0xEC};
        #define AUTH_HOOK(rva, fn, bytes, original) \
            { const auto s = hook::Install(reinterpret_cast<void*>(base + rva), fn, bytes, sizeof(bytes), original); \
              if (s != hook::Status::Installed) return s; }
        AUTH_HOOK(0x165FFA0, Begin, begin, g_begin);
        AUTH_HOOK(0x1528490, Signed, signedIn, g_signed);
        AUTH_HOOK(0x17EC930, Status, status, g_status);
        AUTH_HOOK(0x12A1EB0, Stats, stats, g_stats);
        AUTH_HOOK(0x1666030, PlatformId, platformId, g_platformId);
        AUTH_HOOK(0x1665C80, PlatformName, platformName, g_platformName);
        AUTH_HOOK(0x16649F0, Error, error, g_error);
        AUTH_HOOK(0x1662C40, ConnectPlatformClient, connect, g_connect);
        #undef AUTH_HOOK
        const auto boot = UseStockOfflineBootCallback();
        if (boot != hook::Status::Installed) return boot;
        g_enabled.store(true, std::memory_order_release);
        LOG_INFO("Auth", "8 verified Replay hooks installed; external client wait disabled for local startup; waiting for BeginAuth RVA 0x165FFA0");
        return hook::Status::Installed;
    }

    void LogSnapshot()
    {
        if (!g_enabled.load(std::memory_order_acquire)) return;
        const uint32_t encoded = Read<uint32_t>(kAuth + 0x2F4);
        const uint32_t key = Read<uint32_t>(kAuth + 0x2FC);
        const uint8_t shift = Read<uint8_t>(kAuth + 0x2F8) & 31;
        const bool owns = (TransformOwnership(encoded, g_base + kAuth + 0x2F4, key) >> shift) == 1;
        LOG_INFO("Auth", "snapshot: ready=%d begin=%llu dw_queries=%llu stats_queries=%llu bnet=%d user=%d identity_match=%d finished=%u owns=%d content=%u offline_stats_fetched=%u error=%08X",
            int(g_ready.load()), g_beginCalls.load(), g_dwCalls.load(), g_statsCalls.load(), Read<int>(kAuth), Read<int>(kUser),
            int(Read<uint64_t>(kUser + 0x90) == g_identity.xuid && Read<uint64_t>(kUser + 0xB8) == g_identity.xuid && Read<uint64_t>(kPlatformXuid) == g_identity.xuid),
            Read<uint8_t>(kAuth + 0x2D0), int(owns), Read<uint8_t>(kContentFinished), Read<uint8_t>(kOfflineStatsFetched), Read<uint32_t>(kLastBnetError));
    }
}
