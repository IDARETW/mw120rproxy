#include "log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <atomic>
#include "engine_console_gate.h"

// Lean logger: file + OutputDebugString + plain console. No pinned-prompt / VT-color polish
// (mw164proxy grew that later for its interactive console feature; out of scope for this
// starter scaffold — add it back the same way if/when a console command feature lands here).
namespace {
std::mutex g_mtx;
std::wstring g_logPath;
std::wstring g_tracePath;
bool g_console = false;
bool g_inited = false;
SRWLOCK g_engineLock = SRWLOCK_INIT;
char g_engineLines[256][2048]{};
unsigned g_engineHead = 0, g_engineCount = 0;
std::atomic<unsigned> g_consoleSuppressed{0};
engineconsole::Gate g_engineGate;
HANDLE g_engineEvent = nullptr;
std::atomic<bool> g_engineConsoleReady{false};

DWORD WINAPI EngineConsoleWorker(void*) {
    uint64_t lastReport = 0;
    for (;;) {
        WaitForSingleObject(g_engineEvent, 250);
        for (unsigned batch = 0; batch < 256; ++batch) {
            char line[2048]{};
            AcquireSRWLockExclusive(&g_engineLock);
            if (!g_engineCount) {
                ReleaseSRWLockExclusive(&g_engineLock);
                break;
            }
            memcpy(line, g_engineLines[g_engineHead], sizeof(line));
            g_engineHead = (g_engineHead + 1) % 256;
            --g_engineCount;
            ReleaseSRWLockExclusive(&g_engineLock);
            fputs(line, stdout);
        }
        const auto now = GetTickCount64();
        if (now - lastReport >= 1000) {
            lastReport = now;
            const auto count = g_consoleSuppressed.exchange(0);
            if (count)
                fprintf(stdout, "[Engine] %u repeated/burst messages omitted from this window\n",
                        count);
        }
        fflush(stdout);
    }
}

void Timestamp(char* out, size_t n) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(out, n, _TRUNCATE, "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond,
                st.wMilliseconds);
}
}

namespace log120r {
void Init(HMODULE self, bool openConsole) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_inited)
        return;
    g_inited = true;

    // Log file next to the DLL.
    wchar_t path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(self, path, MAX_PATH);
    if (len && len < MAX_PATH) {
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash)
            slash[1] = 0;
        g_logPath = std::wstring(path) + L"mw120rproxy.log";
        g_tracePath = std::wstring(path) + L"mw120rproxy.trace.log";
        // Truncate at startup so each launch has a fresh log.
        if (FILE* f = nullptr; _wfopen_s(&f, g_logPath.c_str(), L"w") == 0 && f)
            fclose(f);
        if (FILE* f = nullptr; _wfopen_s(&f, g_tracePath.c_str(), L"w") == 0 && f)
            fclose(f);
    }

    g_console = openConsole;
    if (g_console && !GetConsoleWindow()) {
        if (AllocConsole()) {
            FILE* dummy = nullptr;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
            SetConsoleTitleW(L"mw120rproxy");
        }
    } else if (g_console) {
        // A console already exists: bind stdout to it.
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
    }
    if (g_console) {
        // Console text selection must not pause development output/gameplay.
        DWORD mode = 0;
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        if (GetConsoleMode(input, &mode))
            SetConsoleMode(input, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
        g_engineEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_engineEvent) {
            // Match the proxy's process-lifetime hooks: keep worker code
            // resident even if an external caller releases a DLL reference.
            HMODULE pinned = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   reinterpret_cast<LPCWSTR>(&EngineConsoleWorker), &pinned)) {
                HANDLE thread = CreateThread(nullptr, 0, EngineConsoleWorker, nullptr, 0, nullptr);
                if (thread) {
                    CloseHandle(thread);
                    g_engineConsoleReady.store(true, std::memory_order_release);
                }
            }
        }
    }
}

void EngineConsole(unsigned channel, int flags, const char* text) {
    if (!g_engineConsoleReady.load(std::memory_order_acquire) || !text || !*text)
        return;
    const DWORD saved = GetLastError();
    const auto now = GetTickCount64();
    const auto hash = engineconsole::Hash(channel, flags, text);
    if (!TryAcquireSRWLockExclusive(&g_engineLock)) {
        ++g_consoleSuppressed;
        SetLastError(saved);
        return;
    }
    if (g_engineCount == 256 || !g_engineGate.Accept(hash, flags == 3, now)) {
        ++g_consoleSuppressed;
        ReleaseSRWLockExclusive(&g_engineLock);
        SetLastError(saved);
        return;
    }
    char clean[1750]{};
    size_t n = 0;
    for (size_t i = 0; text[i] && n < sizeof(clean) - 1; ++i) {
        if (text[i] == '^' && text[i + 1] >= '0' && text[i + 1] <= '9') {
            ++i;
            continue;
        }
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 32 || c == '\n' || c == '\t')
            clean[n++] = static_cast<char>(c);
    }
    char ts[32];
    Timestamp(ts, sizeof(ts));
    auto* line = g_engineLines[(g_engineHead + g_engineCount) % 256];
    _snprintf_s(line, 2048, _TRUNCATE, "[%s] [Engine ch=0x%X %s] %s%s", ts, channel,
                flags == 3   ? "ERROR"
                : flags == 2 ? "WARN"
                             : "DEBUG",
                clean, n && clean[n - 1] == '\n' ? "" : "\n");
    ++g_engineCount;
    ReleaseSRWLockExclusive(&g_engineLock);
    SetEvent(g_engineEvent);
    SetLastError(saved);
}

void Linef(const char* fmt, ...) {
    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    char ts[32];
    Timestamp(ts, sizeof(ts));

    char line[2176];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[%s] %s\n", ts, body);

    std::lock_guard<std::mutex> lk(g_mtx);
    OutputDebugStringA(line);
    if (g_console) {
        fputs(line, stdout);
        fflush(stdout);
    }
    if (!g_logPath.empty()) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, g_logPath.c_str(), L"a") == 0 && f) {
            fputs(line, f);
            fclose(f);
        }
    }
}

void Shutdown() {}
}

// Verbose per-dvar trace (Dvar_RegisterVariant firehose) -> mw120rproxy.trace.log only.
// The trace file is opened once and kept open (thousands of dvars register, so a
// per-call fopen/fclose would noticeably drag boot); flushed each line so it survives a crash.
// Guarded by g_mtx (the same mutex Linef uses) — g_tracePath is only ever written once, inside
// Init(), so sharing the lock with Linef is simplest and avoids a second mutex racing the write.
namespace {
FILE* g_traceFile = nullptr;
bool g_traceOpenTried = false;
}
namespace logger {
void Trace(const char* tag, const char* fmt, ...) {
    char body[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_tracePath.empty())
        return;
    if (!g_traceFile && !g_traceOpenTried) {
        g_traceOpenTried = true;
        _wfopen_s(&g_traceFile, g_tracePath.c_str(), L"a"); // Init() already truncated it
    }
    if (g_traceFile) {
        fprintf(g_traceFile, "[%s] %s\n", tag ? tag : "", body);
        fflush(g_traceFile);
    }
}
}
