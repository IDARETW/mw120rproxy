#include "diagnostics.h"
#include "state.h"
#include "logger.h"
#include "replay_symbols.h"
#include "asset_context.h"
#include <atomic>
#include <cstdio>

namespace {
HANDLE g_file = INVALID_HANDLE_VALUE;
HANDLE g_archive = INVALID_HANDLE_VALUE;
thread_local bool g_capturing = false;
std::atomic<unsigned> g_records{0};
std::atomic<unsigned> g_breakpoints{0};
void WriteCapture(const char* data, DWORD size) {
    DWORD written = 0;
    if (g_file != INVALID_HANDLE_VALUE)
        WriteFile(g_file, data, size, &written, nullptr);
    if (g_archive != INVALID_HANDLE_VALUE)
        WriteFile(g_archive, data, size, &written, nullptr);
}
void WriteFrame(unsigned index, uintptr_t pc, uintptr_t sp) {
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery((void*)pc, &mbi, sizeof(mbi));
    char module[MAX_PATH]{};
    if (mbi.Type == MEM_IMAGE)
        GetModuleFileNameA((HMODULE)mbi.AllocationBase, module, MAX_PATH);
    char line[1024]{};
    const auto gameBase = state::moduleBase.load();
    uintptr_t symbolOffset = 0;
    const char* symbol = (pc >= gameBase && pc - gameBase < state::imageSize.load())
                             ? replaysymbols::Find(pc - gameBase, symbolOffset)
                             : nullptr;
    const int n = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "frame[%u] tid=%lu pc=%016llX sp=%016llX module=%s base=%p rva=%llX symbol=%s+0x%llX\r\n",
        index, GetCurrentThreadId(), (unsigned long long)pc, (unsigned long long)sp, module,
        mbi.AllocationBase, (unsigned long long)(pc - (uintptr_t)mbi.AllocationBase),
        symbol ? symbol : "<unresolved>", symbolOffset);
    if (n > 0)
        WriteCapture(line, n);
}

void UnwindFrames(const CONTEXT* source) {
    // A bad stack must not cause this diagnostic to consume the game exception.
    __try {
        CONTEXT context = *source;
        for (unsigned frame = 0; frame < 32; ++frame) {
            WriteFrame(frame, context.Rip, context.Rsp);
            const DWORD64 previousSp = context.Rsp;
            DWORD64 imageBase = 0;
            const PRUNTIME_FUNCTION function =
                context.Rip ? RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr) : nullptr;
            if (function) {
                void* handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context,
                                 &handlerData, &establisher, nullptr);
            } else {
                SIZE_T read = 0;
                DWORD64 caller = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (void*)context.Rsp, &caller,
                                       sizeof(caller), &read))
                    break;
                context.Rip = caller;
                context.Rsp += sizeof(caller);
            }
            if (!context.Rip || context.Rsp <= previousSp)
                break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void InspectStartupCallback(const CONTEXT* c) {
    DWORD64 caller = 0;
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (void*)c->Rsp, &caller, sizeof(caller), &read))
        return;
    const uintptr_t base = state::moduleBase.load();
    if (caller != base + 0x3153FE)
        return;
    // In sub_1403061A0 the indirect call at +0xF257 uses [rsp+0x1EC8].
    // At the exception, the call's return address adds eight bytes to that offset.
    const size_t offsets[] = {0x15A8, 0x1ED0, 0x1188, 0xBB0, 0x480};
    char line[1024]{};
    for (size_t offset : offsets) {
        DWORD64 value = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)(c->Rsp + offset), &value, sizeof(value),
                               &read))
            continue;
        const int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "bootstrap_stack+%llX=%016llX\r\n",
                                  (unsigned long long)offset, value);
        if (n > 0)
            WriteCapture(line, n);
        if (offset == 0x1ED0 && value) {
            WriteFrame(99, value, c->Rsp);
            unsigned char bytes[32]{};
            if (ReadProcessMemory(GetCurrentProcess(), (void*)value, bytes, sizeof(bytes), &read)) {
                int pos = sprintf_s(line, "callback_bytes=");
                for (size_t i = 0; i < read; ++i)
                    pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ", bytes[i]);
                pos += sprintf_s(line + pos, sizeof(line) - pos, "\r\n");
                WriteCapture(line, pos);
            }
        }
    }
}

LONG CALLBACK ExceptionObserver(EXCEPTION_POINTERS* pointers) {
    const auto* record = pointers->ExceptionRecord;
    const DWORD savedError = GetLastError();
    if (g_capturing)
        return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = record->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_IN_PAGE_ERROR && code != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;
    if (g_file == INVALID_HANDLE_VALUE)
        return EXCEPTION_CONTINUE_SEARCH;
    if (code == EXCEPTION_BREAKPOINT ? g_breakpoints.fetch_add(1) >= 16
                                     : g_records.fetch_add(1) >= 32)
        return EXCEPTION_CONTINUE_SEARCH;
    g_capturing = true;
    const auto* c = pointers->ContextRecord;
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(record->ExceptionAddress, &mbi, sizeof(mbi));
    char line[2048]{};
    const auto base = state::moduleBase.load();
    const auto size = state::imageSize.load();
    const bool inGame = c->Rip >= base && c->Rip - base < size;
    const int n = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "FIRST-CHANCE (may be handled by game) code=%08lX tid=%lu address=%p allocation=%p game_rva=%llX\r\n"
        "RIP=%016llX RSP=%016llX RBP=%016llX RCX=%016llX RDX=%016llX R8=%016llX R9=%016llX RAX=%016llX\r\n"
        "RBX=%016llX RSI=%016llX RDI=%016llX R10=%016llX R11=%016llX R12=%016llX R13=%016llX R14=%016llX R15=%016llX EFLAGS=%08lX\r\n"
        "operation=%llu referenced_address=%016llX hook_state=%d bool_calls=%llu variant_calls=%llu\r\n",
        code, GetCurrentThreadId(), record->ExceptionAddress, mbi.AllocationBase,
        inGame ? c->Rip - base : ~0ULL, c->Rip, c->Rsp, c->Rbp, c->Rcx, c->Rdx, c->R8, c->R9,
        c->Rax, c->Rbx, c->Rsi, c->Rdi, c->R10, c->R11, c->R12, c->R13, c->R14, c->R15, c->EFlags,
        record->NumberParameters > 0 ? record->ExceptionInformation[0] : 0,
        record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0, state::hookState.load(),
        state::boolDvarsSeen.load(), state::dvarsSeen.load());
    if (n > 0)
        WriteCapture(line, n);
    const auto& asset = assetcontext::current;
    if (asset.active) {
        const int count = _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "ACTIVE ASSET type=%d name='%s' asset=%016llX phase=%s HavokData=%016llX HavokBytes=%u\r\n",
            asset.type, asset.name, asset.asset, asset.phase, asset.shapeData, asset.shapeBytes);
        if (count > 0)
            WriteCapture(line, count);
    }
    unsigned char instruction[16]{};
    SIZE_T instructionBytes = 0;
    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(c->Rip), instruction,
                          sizeof(instruction), &instructionBytes)) {
        int pos = sprintf_s(line, "fault_instruction_bytes=");
        for (size_t i = 0; i < instructionBytes; ++i)
            pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ", instruction[i]);
        pos += sprintf_s(line + pos, sizeof(line) - pos, "\r\n");
        WriteCapture(line, pos);
    }
    UnwindFrames(c);
    InspectStartupCallback(c);
    unsigned long long stack[32]{};
    SIZE_T read = 0;
    if (ReadProcessMemory(GetCurrentProcess(), (void*)c->Rsp, stack, sizeof(stack), &read)) {
        for (size_t i = 0; i < read / sizeof(stack[0]); ++i) {
            const int length =
                _snprintf_s(line, sizeof(line), _TRUNCATE, "stack+%03llX=%016llX\r\n",
                            (unsigned long long)i * 8, stack[i]);
            if (length > 0)
                WriteCapture(line, length);
        }
    }
    FlushFileBuffers(g_file);
    if (g_archive != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_archive);
    g_capturing = false;
    SetLastError(savedError);
    return EXCEPTION_CONTINUE_SEARCH;
}
}

namespace diagnostics {
void Initialize(HMODULE self) {
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash)
        return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"mw120rproxy.exceptions.log");
    g_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME stamp{};
    GetSystemTime(&stamp);
    wchar_t archiveName[128]{};
    swprintf_s(archiveName, L"mw120rproxy.crash.%04u%02u%02u-%02u%02u%02u-%lu.log", stamp.wYear,
               stamp.wMonth, stamp.wDay, stamp.wHour, stamp.wMinute, stamp.wSecond,
               GetCurrentProcessId());
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), archiveName);
    g_archive = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!AddVectoredExceptionHandler(1, &ExceptionObserver)) {
        const DWORD error = GetLastError();
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
        if (g_archive != INVALID_HANDLE_VALUE)
            CloseHandle(g_archive);
        g_archive = INVALID_HANDLE_VALUE;
        LOG_WARN("Diagnostics", "exception observer unavailable (%lu)", error);
    }
}
}
