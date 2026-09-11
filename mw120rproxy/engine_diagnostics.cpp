#include "engine_diagnostics.h"
#include "replay_bindings.h"
#include "safemem.h"
#include "logger.h"
#include "replay_symbols.h"
#include "asset_context.h"
#include "engine_console_gate.h"
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <intrin.h>

namespace {
HANDLE g_file = INVALID_HANDLE_VALUE;
SRWLOCK g_lock = SRWLOCK_INIT;
SRWLOCK g_printGateLock = SRWLOCK_INIT;
engineconsole::Gate g_printGate;
std::atomic<unsigned> g_suppressedPrints{0};
unsigned long long g_bytes = 0;
bool g_limitReported = false;
std::atomic<unsigned> g_printErrors{0}, g_discErrors{0}, g_comErrors{0}, g_fatalErrors{0};
uintptr_t g_base = 0;
using Print = uintptr_t (*)(unsigned, const char*, int);
using DiscError = uintptr_t (*)(uintptr_t, unsigned, unsigned);
using ComError = void (*)(int, const char*, va_list);
using FatalText = void (*)(const char*);
std::atomic<Print> g_print{nullptr};
std::atomic<DiscError> g_disc{nullptr};
std::atomic<ComError> g_comError{nullptr};
std::atomic<FatalText> g_fatalText{nullptr};
using LinkAsset = uintptr_t (*)(int, uintptr_t*);
std::atomic<LinkAsset> g_linkAsset{nullptr};

// This file never uses the ordinary logger, native print functions or Lua.
// Keep fatal evidence available even after the routine debug-output budget.
void Write(bool error, const char* format, ...) {
    if (g_file == INVALID_HANDLE_VALUE)
        return;
    char line[17408]{};
    int prefix = sprintf_s(line, "[%llu][tid=%lu] ", GetTickCount64(), GetCurrentThreadId());
    va_list args;
    va_start(args, format);
    _vsnprintf_s(line + prefix, sizeof(line) - prefix, _TRUNCATE, format, args);
    va_end(args);
    const DWORD size = static_cast<DWORD>(strlen(line));
    AcquireSRWLockExclusive(&g_lock);
    DWORD written = 0;
    if (error || g_bytes < 64ull * 1024 * 1024) {
        WriteFile(g_file, line, size, &written, nullptr);
        g_bytes += written;
    } else if (!g_limitReported) {
        const char limit[] =
            "Routine engine output capped at 64 MiB; fatal diagnostics remain enabled.\r\n";
        WriteFile(g_file, limit, sizeof(limit) - 1, &written, nullptr);
        g_limitReported = true;
    }
    if (error)
        FlushFileBuffers(g_file);
    ReleaseSRWLockExclusive(&g_lock);
    if (error)
        log120r::EngineConsole(0, 3, line);
}
void Stack() {
    void* frames[32]{};
    const USHORT count = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        const auto pc = reinterpret_cast<uintptr_t>(frames[i]);
        if (pc >= g_base && pc - g_base < 0x1324B000) {
            uintptr_t offset = 0;
            const char* name = replaysymbols::Find(pc - g_base, offset);
            if (name)
                Write(true, "  frame[%u] game RVA 0x%llX %s+0x%llX\r\n", i, pc - g_base, name,
                      offset);
            else
                Write(true, "  frame[%u] game RVA 0x%llX\r\n", i, pc - g_base);
        } else {
            MEMORY_BASIC_INFORMATION info{};
            VirtualQuery(frames[i], &info, sizeof(info));
            char path[MAX_PATH]{};
            if (info.Type == MEM_IMAGE)
                GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase), path, MAX_PATH);
            Write(true, "  frame[%u] pc=%p module=%s offset=0x%llX\r\n", i, frames[i], path,
                  pc - reinterpret_cast<uintptr_t>(info.AllocationBase));
        }
    }
}
template <class T> T Read(uintptr_t address) {
    T value{};
    safemem::ReadBytes(reinterpret_cast<void*>(address), &value, sizeof(value));
    return value;
}
uintptr_t LinkAssetEntry(int type, uintptr_t* header) {
    const DWORD saved = GetLastError();
    const auto previous = assetcontext::current;
    auto& context = assetcontext::current;
    context = {};
    context.active = true;
    context.type = type;
    context.phase = "DB_LinkXAssetEntry";
    context.asset = Read<uintptr_t>(reinterpret_cast<uintptr_t>(header));
    const auto name = Read<uintptr_t>(context.asset);
    safemem::ReadString(reinterpret_cast<const char*>(name), context.name, sizeof(context.name));
    if (type == 23 || type == 29) {
        const auto field = type == 29 ? 0x150 : 0xC0;
        context.shapeData = Read<uintptr_t>(context.asset + field);
        context.shapeBytes = Read<unsigned>(context.asset + field - 8);
        Write(false, "ASSET BEGIN type=%d name='%s' asset=%p HavokData=%p HavokBytes=%u\r\n", type,
              context.name, reinterpret_cast<void*>(context.asset),
              reinterpret_cast<void*>(context.shapeData), context.shapeBytes);
    } else if (type == 61) {
        Write(
            false,
            "ASSET BEGIN NetConstStrings name='%s' stringType=%u sourceType=%u flags=0x%X entries=%u\r\n",
            context.name, Read<unsigned>(context.asset + 8), Read<unsigned>(context.asset + 12),
            Read<unsigned>(context.asset + 16), Read<unsigned>(context.asset + 20));
    }
    SetLastError(saved);
    const auto result = g_linkAsset.load(std::memory_order_acquire)(type, header);
    const DWORD after = GetLastError();
    assetcontext::current = previous;
    SetLastError(after);
    return result;
}
uintptr_t PrintMessage(unsigned channel, const char* text, int flags) {
    const DWORD saved = GetLastError();
    char message[16385]{};
    safemem::ReadString(text, message, sizeof(message));
    // Com_PrintError passes flags=3; channel is the output category.
    const bool error = flags == 3;
    bool record = error;
    if (!error && TryAcquireSRWLockExclusive(&g_printGateLock)) {
        record = g_printGate.Accept(engineconsole::Hash(channel, flags, message), false,
                                    GetTickCount64());
        ReleaseSRWLockExclusive(&g_printGateLock);
    }
    if (record) {
        const auto suppressed = g_suppressedPrints.exchange(0, std::memory_order_relaxed);
        if (suppressed)
            Write(false, "Omitted %u repeated/burst non-error messages.\r\n", suppressed);
        Write(false, "PRINT channel=0x%X flags=%d %s%s", channel, flags, message,
              *message && message[strlen(message) - 1] == '\n' ? "" : "\r\n");
    } else {
        g_suppressedPrints.fetch_add(1, std::memory_order_relaxed);
    }
    log120r::EngineConsole(channel, flags, message);
    if (error && g_printErrors.fetch_add(1) < 16)
        Stack();
    SetLastError(saved);
    return g_print.load(std::memory_order_acquire)(channel, text, flags);
}
uintptr_t Disc(uintptr_t reader, unsigned major, unsigned minor) {
    const DWORD saved = GetLastError();
    if (g_discErrors.fetch_add(1) < 16) {
        const auto descriptor = Read<uintptr_t>(reader + 0xB0);
        char filename[128]{};
        safemem::ReadString(reinterpret_cast<const char*>(descriptor), filename, sizeof(filename));
        Write(true, "DB_DiscError [%u.%u] file='%s' reader=%p descriptor=%p\r\n", major, minor,
              filename, reinterpret_cast<void*>(reader), reinterpret_cast<void*>(descriptor));
        const auto state = Read<uintptr_t>(reader);
        Write(
            true,
            "  sizeRead=%llu sizeInflated=%llu sizePassedToInflator=%llu fileSize=%llu signed=%u\r\n",
            Read<uint64_t>(reader + 0xB8), Read<uint64_t>(reader + 0xC0),
            Read<uint64_t>(reader + 0xC8), Read<uint64_t>(reader + 0xD0),
            Read<uint8_t>(descriptor + 0x50));
        const auto index = Read<unsigned>(reader + 0xAC), count = Read<unsigned>(reader + 0xA8);
        if (index < count && count <= 20) {
            const auto buffer = Read<uintptr_t>(reader + 8 + index * 8);
            Write(true,
                  "  readBuffer=%p requested=%llu offset=%llu state=%u actual=%llu result=%u\r\n",
                  reinterpret_cast<void*>(buffer), Read<uint64_t>(buffer + 8),
                  Read<uint64_t>(buffer + 16), Read<unsigned>(buffer + 24),
                  Read<uint64_t>(buffer + 32), Read<unsigned>(buffer + 40));
        }
        Write(
            true,
            "  decoder avail_in=%llu total_in=%llu avail_out=%llu total_out=%llu compressor=%u authKind=%u\r\n",
            Read<uint64_t>(state + 0x8E8F0), Read<uint64_t>(state + 0x8E8F8),
            Read<uint64_t>(state + 0x8E908), Read<uint64_t>(state + 0x8E910),
            Read<unsigned>(state + 0x8E918), Read<unsigned>(state + 0x8E980));
        Stack();
    }
    SetLastError(saved);
    // Original prints the complete native decoder/auth state through PrintMessage.
    return g_disc.load(std::memory_order_acquire)(reader, major, minor);
}
void Com(int code, const char* format, va_list args) {
    const DWORD saved = GetLastError();
    if (g_comErrors.fetch_add(1) < 16) {
        char text[2048]{};
        safemem::ReadString(format, text, sizeof(text));
        Write(true, "Com_Error_Internal code=%d format='%s' (native formatting follows)\r\n", code,
              text);
        Stack();
    }
    SetLastError(saved);
    g_comError.load(std::memory_order_acquire)(code, format,
                                               args); // va_list is never consumed here
}
void Fatal(const char* text) {
    const DWORD saved = GetLastError();
    if (g_fatalErrors.fetch_add(1) < 16) {
        char message[4096]{};
        safemem::ReadString(text, message, sizeof(message));
        Write(true, "Sys_Error formatted text: %s\r\n", message);
        Stack();
    }
    SetLastError(saved);
    g_fatalText.load(std::memory_order_acquire)(text);
}
}

namespace enginediag {
bool Initialize(HMODULE self) {
    wchar_t path[MAX_PATH]{};
    const auto size = GetModuleFileNameW(self, path, MAX_PATH);
    if (!size || size >= MAX_PATH)
        return false;
    auto* slash = wcsrchr(path, L'\\');
    if (!slash)
        return false;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"mw120rproxy.engine.log");
    g_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_file != INVALID_HANDLE_VALUE;
}
hook::Status Install(uintptr_t base) {
    g_base = base;
    auto status =
        hook::Install(reinterpret_cast<void*>(base + replay::PrintMessage.rva), &PrintMessage,
                      replay::PrintMessage.bytes, replay::PrintMessage.size, g_print);
    if (status != hook::Status::Installed)
        return status;
    status = hook::Install(reinterpret_cast<void*>(base + replay::DiscError.rva), &Disc,
                           replay::DiscError.bytes, replay::DiscError.size, g_disc);
    if (status != hook::Status::Installed)
        return status;
    status =
        hook::Install(reinterpret_cast<void*>(base + replay::ComErrorInternal.rva), &Com,
                      replay::ComErrorInternal.bytes, replay::ComErrorInternal.size, g_comError);
    if (status != hook::Status::Installed)
        return status;
    status = hook::Install(reinterpret_cast<void*>(base + replay::FatalText.rva), &Fatal,
                           replay::FatalText.bytes, replay::FatalText.size, g_fatalText);
    if (status != hook::Status::Installed)
        return status;
    status =
        hook::Install(reinterpret_cast<void*>(base + replay::LinkAssetEntry.rva), &LinkAssetEntry,
                      replay::LinkAssetEntry.bytes, replay::LinkAssetEntry.size, g_linkAsset);
    if (status == hook::Status::Installed)
        Write(
            false,
            "Replay engine output, named error call chains and per-thread asset context installed.\r\n");
    return status;
}
}
